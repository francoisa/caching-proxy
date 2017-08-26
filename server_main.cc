#include "server_main.h"
#include "http_caching_proxy.h"
#include "seastate.h"

#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <memory>
#include <future>
#include <chrono>
#include <ctime>
#include <iomanip>

static const int BUFSIZE   = 8096;
static const int HEADER    =   45;
static const int FORBIDDEN =  403;
static const int NOTFOUND  =  404;

void cleanup(std::unique_ptr<ServerMain>& up) {
  ServerMain* sm = up.release();
  delete sm;
}

bool ServerMain::get_response(const std::string& bufStr, int& code) const {
  auto pos = bufStr.find("HTTP/");
  if (pos != std::string::npos && pos < 10) {
    auto space_pos_1 = bufStr.find(' ', pos);
    auto space_pos_2 = bufStr.find(' ', space_pos_1 + 1);
    std::string codeStr = bufStr.substr(space_pos_1+1, space_pos_2);
    std::istringstream iss(codeStr);
    iss >> code;
    return true;
  }
  else {
    return false;
  }
}

int ServerMain::connect(const std::string& host, const std::string& port) const {
  int hit = threadArgs.hit;
  int sock = 0;
  addrinfo* result;
  std::unique_ptr<addrinfo> hints{new addrinfo};
  memset(hints.get(), 0, sizeof(addrinfo));
  hints->ai_family = AF_INET;
  hints->ai_socktype = SOCK_STREAM;
  hints->ai_protocol = IPPROTO_TCP;
  getaddrinfo(host.c_str(), port.c_str(), hints.get(), &result);
  const addrinfo* s = nullptr;
  std::ostringstream oss;
  for (s = result; s != nullptr; s = s->ai_next) {
    if ((sock = socket(s->ai_family, s->ai_socktype, s->ai_protocol)) < 0) {
      oss << "Can't create socket for: " << host << ":" << port;
      logger(ERROR, "connect", oss, sock, hit);
    }
    else if (::connect(sock, s->ai_addr, s->ai_addrlen) < 0) {
      oss << "Can't connect to host: " << host << ":" << port;
      logger(ERROR, "connect", oss, sock, hit);
    }
    break;
  }
  freeaddrinfo(result);
  if (s == nullptr) {
    oss << "Can't resolve host: " << host << ":" << port << " exiting.";
    logger(ERROR, "connect", oss, sock, hit);
    exit(7);
  }
  return sock;
}

bool ServerMain::forward_data(const std::string& mode, int source, int destination, 
                              int flags, int& code, std::string& body) const {
  int hit = threadArgs.hit;
  ssize_t n;
  static const unsigned short BUFSIZE = 8192;
  char buffer[BUFSIZE];
  memset(buffer, 0, BUFSIZE);
  std::ostringstream oss;
    
  logger(LOG, mode, "start", source, hit);

  // read data from input socket
  if ((n = recv(source, buffer, BUFSIZE, flags)) > 0) {
    oss << "recv " << n << " bytes";
    logger(LOG, mode, oss, source, hit);
    std::string bufStr(buffer, n);
    logger(LOG, mode, bufStr, hit);
    if (get_response(bufStr, code)) {
      oss << "code: " << code;
      logger(LOG, mode, oss, hit);
      body += bufStr;
    }
    else {
      code = -1;
      body += bufStr;
    }
    if (send(destination, buffer, n, 0) < 0) { // send data to output socket
      logger(ERROR, mode, "send", destination, hit);
    }
    oss << "send " << n << " bytes";
    logger(LOG, mode, oss, destination, hit);
  }
    
  bool try_again = false;
  if (n < 0 && errno != EAGAIN) {
    oss << "recv " << n;
    logger(ERROR, mode, oss, destination, hit);
    return try_again;
  }
    
  try_again = (n == 0 || n == BUFSIZE || (flags != 0 && errno == EAGAIN));
  oss << "done with try_again = " << (try_again ? "true" : "false") << " errno = " 
      << errno << " error '" << strerror(errno) << "'";  
  logger(LOG, mode, oss, source, hit);
  return try_again;
}

bool ServerMain::forward_response(int source, int destination, int& code) {
  bool result = forward_data("response", source, destination, 0, code, response);
  return result;
}

bool ServerMain::forward_request(int source, int destination, int flags) {
  int code = -1;
  bool result = forward_data("request", source, destination, flags, code, request);
  return result;
}

ServerMain::ServerMain(const ThreadArgs& ta) : threadArgs(ta) {}

ServerMain::~ServerMain() {
  logger(LOG, "~ServerMain", "dtor", threadArgs.clntSock, threadArgs.hit);
}

void ServerMain::proxy() {
  int hit = threadArgs.hit;
  for (const auto& dest : threadArgs.dests) {
    bool try_again = true;
    int destSock = connect(dest.first, dest.second);
    std::ostringstream oss;
    for (int c = 0; try_again && c < 3; ++c) {
      try_again = forward_request(threadArgs.clntSock, destSock, MSG_DONTWAIT);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    Method method = parse_method(request.c_str(), threadArgs.clntSock);
    int offset = method == Method::GET ? 4 : 5;
    std::string path = parse_path(request.c_str(), BUFSIZE, offset);
    oss << "path: '" << path << "'";
    logger(LOG, "proxy", oss, threadArgs.clntSock, hit);
    SeaState state;
    std::istringstream iss(path);
    uint64_t hash = state.hash(path);
    oss << "hash: " << std::hex << std::setw(16) << std::setfill('0') << hash;
    logger(LOG, "proxy", oss, threadArgs.clntSock, hit);
    if (path == "/getpid") {
      handle_getpid(threadArgs.clntSock);
      shutdown(destSock, SHUT_RDWR); // stop other processes from using socket
      close(destSock);
      break;
    }
    else if (send_response(hash)) {
      shutdown(destSock, SHUT_RDWR); // stop other processes from using socket
      close(destSock);
      break;
    }
    else {
      try_again = true;
      int code = 0;
      for (int c = 0; try_again && c < 3; ++c) {
        try_again = forward_response(destSock, threadArgs.clntSock, code);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      if (code < 399) {
        save_response(hash);
        break;
      }
      else {
        response.clear();
      }
      shutdown(destSock, SHUT_RDWR); // stop other processes from using socket
      close(destSock);
    }
  }
  shutdown(threadArgs.clntSock, SHUT_RDWR); // stop other processes from using socket
  close(threadArgs.clntSock);
  cleanup(up);
}
  
void ServerMain::handle() {
  int hit = threadArgs.hit;

  // Extract socket file descriptor from argument
  int fd = threadArgs.clntSock;

  long ret;
  static char buffer[BUFSIZE+1]; /* static so zero filled */

  ret = read(fd, buffer, BUFSIZE); /* read Web request in one go */
  if (ret == 0 || ret == -1) { /* read failure stop now */
    logger(FORBIDDEN, "failed to read browser request", "", fd);
  }
  if (ret > 0 && ret < BUFSIZE) { /* return code is valid chars */
    buffer[ret] = 0; /* terminate the buffer */
  }
  else {
    buffer[0] = 0;
  }
  logger(HEADER, "Request + Header", buffer, hit++);
  Method method = parse_method(buffer, fd);
  std::map<const char*, std::string> header;
  parse_headers(buffer, header);
  switch (method) {
  case Method::GET:
    handle_get(buffer, header, fd, hit);
    break;
  case Method::POST:
    handle_post(buffer, header, fd, hit);
    break;
  default:
    logger(LOG, "Method", buffer, fd);
  }
  logger(LOG, "ServerMain::main", "finished", hit);
  cleanup(up);
}

Method ServerMain::parse_method(const char* buffer, int fd) {
  static const std::string GET{"GET "};
  static const std::string get{"get "};
  static const std::string POST{"POST "};
  static const std::string post{"post "};
  if (strncmp(buffer, GET.c_str(), GET.size()) == 0 ||
      strncmp(buffer, get.c_str(), get.size()) == 0) {
    logger(LOG, "Method", "GET", fd);
    return Method::GET;
  }
  else if (strncmp(buffer, POST.c_str(), POST.size()) == 0 ||
           strncmp(buffer, post.c_str(), post.size()) == 0) {
    logger(LOG, "Method", "POST", fd);
    return Method::POST;
  }
  logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
  exit(4);
  return Method::GET;
}

void ServerMain::parse_headers(const char* buffer, 
                               std::map<const char*, std::string>& header) {
  int pos = 0;
  const char* request = buffer;
  while(request[0] != '\0' && pos < BUFSIZE) {
    const char* lf = strchr(request, '\n');
    if (lf == nullptr) {
      break;
    }
    pos = lf - request;
    const char* colon = strchr(request, ':');
    if (colon == nullptr) {
      break;
    }
    if ((lf - colon) > 0) {
      auto size = static_cast<std::string::size_type>(colon - request);
      std::string name{request, size};
      colon++;
      while (isspace(colon[0])) {
        colon++;
      }
      size = static_cast<std::string::size_type>(lf - colon);
      std::string value{colon, size};
      while (isspace(value.back())) {
        value.pop_back();
      }
      logger(LOG, "header", name + "=" + value, 0);
      header[name.c_str()] = value;
    }
    pos += (lf - request);
    request = lf + 1;
  }
}

std::string ServerMain::parse_path(const char* buffer, int len, int offset) const {
  int i;
  for (i = offset;i < len; i++) {
    if (buffer[i] == ' ') {
      return std::string(buffer + offset, i - offset);
    }
  }
  return std::string();
}

void ServerMain::handle_getpid(int fd) const {
  int hit = threadArgs.hit;
  std::ostringstream pid;
  pid << getpid();
  std::ostringstream out;
  out << "HTTP/1.1 200 OK\nServer: http_caching_proxy/" << VERSION << ".0\n"
      << "Content-Length: " << pid.str().size() << "\n"
      << "Connection: close\nContent-Type: text/plain\n\n" << pid.str();
  write(fd, out.str().c_str(), out.str().size());
  logger(HEADER, "Response Header", out, fd, hit);
}

void ServerMain::handle_get(char* buffer, std::map<const char*, 
                            std::string>& header, int fd, int hit) {
  std::string::size_type len, buflen;
  int i;
  for (i = 4;i < BUFSIZE; i++) { /* null terminate after the second space */
    if (buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
      buffer[i] = 0;
      break;
    }
  }
  for (int j = 0; j < i-1; j++) { /* check for illegal parent directory use .. */
    if (buffer[j] == '.' && buffer[j+1] == '.') {
      logger(FORBIDDEN, "Parent directory (..) path names not supported",
             buffer, fd, hit);
    }
  }
  if (!strncmp(&buffer[0], "GET /\0", 6) ||
      !strncmp(&buffer[0], "get /\0", 6) ) { /* convert to index file */
    strcpy(buffer, "GET /index.html");
  }
  /* work out the file type and check we support it */
  buflen = strlen(buffer);
  std::string fext;
  for (auto& extension : extensions) {
    len = strlen(extension.ext);
    if (!strncmp(&buffer[buflen-len], extension.ext, len)) {
      fext = extension.filetype;
      break;
    }
  }
  logger(LOG, "Extension", fext, fd, hit);
  if (fext.empty()) {
    std::string path{&buffer[4], strlen(&buffer[4])};
    const auto& td = threadArgs.rest_data.find(path);
    if (td == threadArgs.rest_data.end() && path != "/getpid") {
      logger(NOTFOUND, "path not found", &buffer[5], fd, hit);
    }
    else if (path == "/getpid") {
      handle_getpid(fd);
    }
    else {
      std::ifstream file;
      file.open(threadArgs.rest_data[path]);
      if (!file) {  /* open the file for reading */
        logger(NOTFOUND, "failed to open file", &buffer[5], fd, hit);
      }
      file.seekg(0, std::ios::end);
      std::streampos fsize = file.tellg();
      file.seekg(0, std::ios::beg);
      logger(LOG, "Send", &buffer[5], fd, hit);
      std::ostringstream log;
      log << "HTTP/1.1 200 OK\nServer: mock_rest_api/" << VERSION << ".0\n"
          << "Content-Length: " << fsize << "\n"
          << "Connection: close\nContent-Type: " << fext << "\n\n";
      logger(HEADER, "Response Header", log, fd, hit);
      write(fd, log.str().c_str(), log.str().size());

      /* send file in 8KB block - last block may be smaller */
      while (!file.eof()) {
        int ret = file.readsome(buffer, BUFSIZE);
        if (ret > 0) {
          write(fd, buffer, ret);
        }
        else {
          break;
        }
      }
    }
  }
  else {
    std::ifstream file;
    file.open(&buffer[5]);
    if (!file) {  /* open the file for reading */
      logger(NOTFOUND, "failed to open file", &buffer[5], fd, hit);
    }
    file.seekg(0, std::ios::end);
    std::streampos fsize = file.tellg();
    file.seekg(0, std::ios::beg);
    logger(LOG, "Send", &buffer[5], fd, hit);
    std::ostringstream log;
    log << "HTTP/1.1 200 OK\nServer: mock_rest_api/" << VERSION << ".0\n"
        << "Content-Length: " << fsize << "\n"
        << "Connection: close\nContent-Type: " << fext << "\n\n";
    logger(HEADER, "Response Header", log, fd, hit);
    write(fd, log.str().c_str(), log.str().size());

    /* send file in 8KB block - last block may be smaller */
    while (!file.eof()) {
      int ret = file.readsome(buffer, BUFSIZE);
      if (ret > 0) {
        write(fd, buffer, ret);
      }
      else {
        break;
      }
    }
  }
  close(fd);
}

void ServerMain::handle_post(char* buffer, 
                             const std::map<const char*, std::string>& header,
                             int fd, int hit) {
  int len, buflen;
  int i;
  for (i = 4;i < BUFSIZE; i++) { /* null terminate after the second space to ignore extra stuff */
    if (buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
      buffer[i] = 0;
      break;
    }
  }
  for (int j = 0; j < i-1; j++) { /* check for illegal parent directory use .. */
    if (buffer[j] == '.' && buffer[j+1] == '.') {
      logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd, hit);
    }
  }
  /* work out the file type and check we support it */
  buflen = strlen(buffer);
  std::string fstr;
  for (auto& extension : extensions) {
    len = strlen(extension.ext);
    if (!strncmp(&buffer[buflen-len], extension.ext, len)) {
      fstr = extension.filetype;
      break;
    }
  }
  if (fstr.empty()) {
    logger(FORBIDDEN, "file extension type not supported", buffer, fd, hit);
  }
  close(fd);
}

void ServerMain::save_response(uint64_t hash) const {
  std::ostringstream oss;
  oss << std::hex << std::setw(16) << std::setfill('0') << hash;
  std::ofstream resp(oss.str() + ".res");
  resp << response;
  resp.close();
  std::ofstream req(oss.str() + ".req");
  req << request;
  req.close();
}

bool ServerMain::send_response(uint64_t hash) {
  int hit = threadArgs.hit;
  std::ostringstream oss;
  oss << std::hex << std::setw(16) << std::setfill('0') << hash;
  std::ifstream resp(oss.str() + ".res");
  std::string line;
  std::ostringstream log;
  if (resp.is_open()) {
    response.clear();
    while (getline(resp, line)) {
      response += line + '\n';
    }
    resp.close();
    write(threadArgs.clntSock, response.c_str(), response.size());
    log << "Sent " << response.size() << " bytes";
    logger(LOG, "send_response", log, threadArgs.clntSock, hit);
    return true;
  }
  else {
    log << "Response file for " << oss.str() << " not found";
    logger(LOG, "send_response", log, threadArgs.clntSock, hit);
    return false;
  }
}
