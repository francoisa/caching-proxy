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

static const unsigned short BUFSIZE = 8192;
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

bool ServerMain::recv_request(const std::string& mode, int source,
                              int flags) {
  int hit = threadArgs.hit;
  ssize_t n;
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
    request += bufStr;
  }

  bool try_again = false;
  if (n < 0 && errno != EAGAIN) {
    oss << "recv " << n;
    logger(ERROR, mode, oss, source, hit);
    return try_again;
  }
  try_again = (n == 0 || n == BUFSIZE || (flags != 0 && errno == EAGAIN));
  oss << "done with try_again = " << (try_again ? "true" : "false")
      << " errno = " << errno << " error '" << strerror(errno) << "'";
  logger(LOG, mode, oss, source, hit);
  return try_again;
}

bool ServerMain::send_request(const std::string& mode, int destination) const {
  int hit = threadArgs.hit;
  ssize_t n;
  std::ostringstream oss;

  logger(LOG, mode, "start", destination, hit);


  // send data to output socket
  if ((n = send(destination, request.c_str(), request.size(), 0)) < 0) {
      logger(ERROR, mode, "send", destination, hit);
  }
  else {
      oss << "send " << request.size() << " bytes";
      logger(LOG, mode, oss, destination, hit);
  }

  bool try_again = false;
  return try_again;
}

static const std::string XFER_ENCODING = "Transfer-Encoding";
static const std::string CHUNKED = "chunked";
static const std::string CONTENT_LEN = "Content-Length";

bool ServerMain::last_chunk(std::string& chunk, unsigned chunk_left) const {
  const std::string ending = "\r\n0\r\n\r\n";
  if (ending.size() > chunk.size()) {
    return false;
  }
  bool is_last_chunk = std::equal(ending.rbegin(), ending.rend(), chunk.rbegin());
  if (is_last_chunk) {
    chunk.erase(chunk.size() - ending.size());
    if (chunk_left < chunk.size()) {
      auto chunk_size_end = chunk.find("\r\n", chunk_left + 2);
      if (chunk_size_end != std::string::npos) {
        auto chunk_size_str = chunk.substr(chunk_left + 2, chunk_size_end - chunk_left - 2);
        std::ostringstream oss;
        oss << "chunk_size_str = " << chunk_size_str<< " chunk.size() = " << chunk.size();
        logger(LOG, "last_chunk", oss, 0, 0);
        chunk.erase(chunk_left, chunk_size_str.size() + 4);
      }
      else {
        logger(LOG, "last_chunk", "could not find 2nd to last chunk length", 0, 0);
      }
    }
  }
  return is_last_chunk;
}

unsigned ServerMain::remove_chunk_info(std::string& chunk, unsigned chunk_left) const {
  if (chunk_left == 0) {
    auto chunk_size_start = chunk.find("\r\n");
    unsigned chunk_size;
    if (chunk_size_start == 0) {
      auto chunk_size_end = chunk.find("\r\n", 2);
      auto chunk_size_str = chunk.substr(2, chunk_size_end - 2);
      std::istringstream iss(chunk_size_str);
      iss >> std::hex >> chunk_size;
      chunk_left = chunk_size - chunk.size();
      std::ostringstream oss;
      oss << "chunk_size_str = " << chunk_size_str << " chunk_size = " << chunk_size;
      logger(LOG, "remove_chunk_info", oss, 0, 0);
      chunk.erase(0, chunk_size_str.size() + 4);
    }
    else {
      std::ostringstream oss;
      oss << "chunk_left = 0 but chunk_size_start = " << chunk_size_start;
      logger(LOG, "remove_chunk_info", oss, 0, 0);
    }
  }
  else if (chunk_left < chunk.size()) {
    unsigned chunk_size;
    auto chunk_size_end = chunk.find("\r\n", chunk_left + 2);
    auto chunk_size_str = chunk.substr(chunk_left + 2, chunk_size_end - (chunk_left + 2));
    std::istringstream iss(chunk_size_str);
    iss >> std::hex >> chunk_size;
    std::ostringstream oss;
    oss << "chunk_size_str = " << chunk_size_str<< " chunk_size = " << chunk_size;
    logger(LOG, "remove_chunk_info", oss, 0, 0);
    chunk.erase(chunk_left, chunk_size_str.size() + 4); 
    chunk_left = chunk_size - (chunk.size() - chunk_left);
  }
  return chunk_left;
}

unsigned ServerMain::remove_chunk_header_info(std::string& chunk) const {
  auto xfer_encoding_start = chunk.find(XFER_ENCODING + ":");
  auto xfer_encoding_end = chunk.find(CHUNKED + "\r\n", xfer_encoding_start) + 
    CHUNKED.size() + 2;
  chunk.erase(xfer_encoding_start, xfer_encoding_end - xfer_encoding_start);
  auto chunk_size_start = chunk.find("\r\n\r\n") + 4;
  auto chunk_size_end = chunk.find("\r\n", chunk_size_start);
  std::istringstream iss(chunk.substr(chunk_size_start, chunk_size_end - chunk_size_start));
  unsigned chunk_size;
  iss >> std::hex >> chunk_size;
  chunk.erase(chunk_size_start, 2 + chunk_size_end - chunk_size_start);
  return chunk_size - (chunk.size() - chunk_size_start);
}

int ServerMain::get_buffer_content_length(const std::string& chunk) const {
  if (chunk.find("HTTP/") == 0) {
    auto chunk_start = chunk.find("\r\n\r\n") + 4;
    return chunk.size() - chunk_start;
  }
  else {
    return chunk.size();
  }
}

bool ServerMain::forward_response(int source, 
                                  int destination, int& code) {
  int hit = threadArgs.hit;
  ssize_t n;
  char buffer[BUFSIZE];
  std::ostringstream oss;
  static const std::string mode = "response";

  logger(LOG, mode, "start", source, hit);

  bool try_again = true;
  bool is_chunked = false;
  response.clear();
  int recv_errno = 0;
  int send_errno = 0;
  std::map<std::string, std::string> headers;
  int content_length = 0;
  int content_left = -1;
  unsigned chunk_left = -1;
  while (try_again) {
    // read data from input socket
    memset(buffer, 0, BUFSIZE);
    errno = 0;
    if ((n = recv(source, buffer, BUFSIZE, 0)) > 0) {
      if (is_chunked) {
        oss << "chunk_left = " << chunk_left << " ";
      }
      oss << "recv = " << n << " bytes";
      logger(LOG, mode, oss, source, hit);
      std::string bufStr(buffer, n);
      if (get_response(bufStr, code)) {
        oss << "code: " << code;
        logger(LOG, mode, oss, source, hit);
      }

      if (code == NOTFOUND) {
        break;
      }
      if (is_chunked) {
        if (last_chunk(bufStr, chunk_left)) {
          logger(LOG, mode, "Last chunk", source, hit);
          is_chunked = false;
        }
        else {
          if (chunk_left >= bufStr.size()) {
            chunk_left -= bufStr.size();
          }
          else {
            chunk_left = remove_chunk_info(bufStr, chunk_left);
          }
          std::ostringstream oss;
          oss << "chunk_left = " << std::dec << chunk_left 
              << " buffer_size = " << bufStr.size();
          logger(LOG, mode, oss, source, hit);
        }
      }
      else {
        parse_headers(buffer, headers);
        if (headers[XFER_ENCODING] == CHUNKED) {
          logger(LOG, mode, XFER_ENCODING + ":" + CHUNKED, source, hit);  
          is_chunked = true;
          chunk_left = remove_chunk_header_info(bufStr);
          std::ostringstream oss;
          oss << "chunk_left = " << std::dec << chunk_left 
              << " buffer_size = " << bufStr.size();
          logger(LOG, mode, oss, source, hit);
        }
        auto content_len = headers.find(CONTENT_LEN);
        if (content_len != headers.end()) {
          std::istringstream iss(content_len->second);
          iss >> content_length;
          int buffer_content_length = get_buffer_content_length(bufStr);
          if (content_left == -1) {
            content_left = content_length - buffer_content_length;
          }
          else {
            content_left -= buffer_content_length;
          }
          logger(LOG, mode, content_len->first + ": " + content_len->second, 
                 source, hit);
          std::ostringstream oss;
          oss << "content_length - buffer_content_length = " << content_left;
          logger(LOG, mode, oss, source, hit);
        }
      }
      logger(LOG, mode, bufStr, source, hit);
      response += bufStr;
      errno = 0;
      // send data to output socket
      if (send(destination, bufStr.c_str(),bufStr.size(), 0) < 0) {
        logger(ERROR, mode, "send", destination, hit);
      }
      send_errno = errno;
      oss << "send " << n << " bytes";
      logger(LOG, mode, oss, destination, hit);
    }
    else {
      recv_errno = errno;
    }

    if (n < 0 && errno != EAGAIN) {
      oss << "recv " << n;
      logger(ERROR, mode, oss, destination, hit);
      return false;
    }
    
    try_again = (n == BUFSIZE) || (is_chunked && n > 0) || (content_left > 0);
    oss << "try_again = " << (try_again ? "true" : "false") << " recv_errno = "
        << recv_errno << " error '" << strerror(recv_errno) << "'"
        << " send_errno = " << send_errno << " error '" << strerror(send_errno) 
        << "'";
    logger(LOG, mode, oss, source, hit);
  }
  return false;
}

ServerMain::ServerMain(const ThreadArgs& ta) : threadArgs(ta) {}

ServerMain::~ServerMain() {
  logger(LOG, "~ServerMain", "dtor", threadArgs.clntSock, threadArgs.hit);
  logger(LOG, "----------------", "------------------", threadArgs.clntSock, threadArgs.hit);
}

void ServerMain::proxy() {
  int hit = threadArgs.hit;
  bool is_first = true;
  int code = 0;
  for (const auto& dest : threadArgs.dests) {
    bool try_again = is_first;
    int destSock = connect(dest.first, dest.second);
    std::ostringstream oss;
    for (int c = 0; try_again && c < 3; ++c) {
      try_again = recv_request("request", threadArgs.clntSock, MSG_DONTWAIT);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    is_first = false;
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
      send_request("request", destSock);
      try_again = forward_response(destSock, threadArgs.clntSock, code);
      if (code < 399) {
        save_response(hash);
        break;
      }
      else {
        if (code == NOTFOUND) {
          logger(LOG, "proxy", "not found", destSock, hit);
        }
        response.clear();
      }
      shutdown(destSock, SHUT_RDWR); // stop other processes from using socket
      close(destSock);
    }
  }
  if (code == NOTFOUND) {
    std::string resp = "HTTP/1.1 404 Not Found\nContent-Length: 43\n" 
      "Connection: close\nContent-Type: application/json\n\n" 
      "{\"code\":404,\"message\":\"HTTP 404 Not Found\"}";
    write(threadArgs.clntSock, resp.c_str(), resp.size());
  }
  shutdown(threadArgs.clntSock, SHUT_RDWR); // stop other processes from using socket
  close(threadArgs.clntSock);
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
                               std::map<std::string, std::string>& header) {
  int pos = 0;
  const char* request = buffer;
  const char* end_headers = "\r\n\r\n";
  const char* end_headers_pos = strstr(buffer, end_headers);
  if (end_headers_pos == nullptr || strncmp(buffer,"HTTP/1",6) != 0) {
    return;
  }
  while(request[0] != '\0' && request < end_headers_pos) {
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
