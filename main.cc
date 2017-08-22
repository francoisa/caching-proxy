#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <iostream>

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include "http_caching_proxy.h"

using namespace std;
namespace po = boost::program_options;

static int listenfd = -1;

void terminate(int signum) {
  if (signum == SIGTERM) {
    if (listenfd > 0) {
      std::ostringstream fd;
      fd << "close socket: " << listenfd;
      logger(LOG, "terminate", fd.str(), getpid());
      close(listenfd);
    }
    logger(LOG, "terminate", "done", getpid());
    exit(0);
  }
}

int connect(const std::string& host, const std::string& port) {
    std::cout << host << ":" << port << std::endl;
    int sock = 0;
    addrinfo* result;
    std::unique_ptr<addrinfo> hints{new addrinfo};
    memset(&hints, 0, sizeof hints);
    hints->ai_family = AF_INET;
    hints->ai_socktype = SOCK_STREAM;
    getaddrinfo(host.c_str(), port.c_str(), hints.get(), &result);
    addrinfo* s = nullptr;
    for (s = result; s != nullptr; s = s->ai_next) {
        if ((sock = socket(s->ai_family, s->ai_socktype, s->ai_protocol)) < 0) {
            std::cout << "ERROR: Can't create destination socket for: " << host
                      << std::endl;
            exit(6);
        }
        if (connect(sock, s->ai_addr, sizeof(s->ai_addrlen)) < 0) {
            std::cout << "ERROR: Can't connect to host: " << host << std::endl;
            exit(8);
        }
    }
    if (s == nullptr) {
        std::cout << "ERROR: Can't resolve host: " << host << std::endl;
        exit(7);
    }
    return sock;
}

static sockaddr_in cli_addr; /* static = initialised to zeros */

int daemon(int port, int& hit, const std::string& data_dir,
           const std::map<std::string, int>& destMap) {
  logger(LOG, "starting", "become daemon", getpid());
  /* Become deamon + unstopable and no zombies children ( = no wait()) */
  if (fork() != 0)
    return 0; /* parent returns OK to shell */
  signal(SIGCLD, SIG_IGN); /* ignore child death */
  signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
  signal(SIGTERM, terminate);
  logger(LOG, "starting", "close open files", getpid());
  for (int i = 0; i < 32; i++)
    close(i); /* close open files */
  setpgrp(); /* break away from process group */
  std::ostringstream portStr;
  portStr << port;
  logger(LOG, "listen on port", portStr.str().c_str(), getpid());
  /* setup the network socket */
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) <0)
    logger(ERROR, "system call", "socket", 0);
  if (port < 0 || port >60000)
    logger(ERROR, "Port", "Invalid number (try 1->60000)", 0);
  static sockaddr_in serv_addr; /* static = initialised to zeros */
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  const sockaddr* servAddr = reinterpret_cast<const sockaddr*>(&serv_addr);
  if (bind(listenfd, servAddr, sizeof(servAddr)) <0)
    logger(ERROR, "system call", "bind", 0);
  if ( listen(listenfd, 64) < 0)
    logger(ERROR, "system call", "listen", 0);
  for (hit = 1; true ;hit++) {
    int socketfd;
    socklen_t length = sizeof(cli_addr);
    if ((socketfd = accept(listenfd,
                           (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "system call", "accept", 0);
    }
    else {
      std::cout << "Log file: " << data_dir << "/http_caching_proxy.log"
                << std::endl;
      proxy(socketfd, hit, destMap); /* never returns */
    }
  }
}

void debug(int port, int& hit, const std::string& data_dir,
           const std::map<std::string, int>& destMap) {
  set_debug();
  signal(SIGTERM, terminate);
  std::ostringstream portStr;
  portStr << port;
  logger(LOG, "listen on port", portStr.str().c_str(), getpid());
  /* setup the network socket */
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) <0)
    logger(ERROR, "system call", "socket", 0);
  if (port < 0 || port >60000)
    logger(ERROR, "Port", "Invalid number (try 1->60000)", 0);
  static sockaddr_in serv_addr; /* static = initialised to zeros */
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  const sockaddr* servAddr = reinterpret_cast<const sockaddr*>(&serv_addr);
  if (bind(listenfd, servAddr, sizeof(servAddr)) <0)
    logger(ERROR, "system call", "bind", 0);
  if ( listen(listenfd, 64) < 0)
    logger(ERROR, "system call", "listen", 0);
  for (hit = 1; true ;hit++) {
    int socketfd;
    socklen_t length = sizeof(cli_addr);
    if ((socketfd = accept(listenfd,
                           (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "system call", "accept", 0);
    }
    else {
      std::cout << "Log file: " << data_dir << "/http_caching_proxy.log"
                << std::endl;
      proxy(socketfd, hit, destMap); /* never returns */
    }
  }
}

void parse_command_line(const po::variables_map& vm, int& port,
                        std::string& data_dir, std::string& rest_data,
                        std::map<std::string, int>& destMap, bool& is_debug) {
  is_debug = vm.count("debug") > 0;
  if (vm.count("port")) {
    std::cout << "Listening on port "
              << vm["port"].as<int>() << std::endl;
  }
  else {
    std::cout << "Listening port was not set." << std::endl;
  }
  if (vm.count("data_dir")) {
    std::cout << "rest api data directory "
              << vm["data_dir"].as<std::string>() << std::endl;
  }
  else {
    std::cout << "Rest api response directory was not set." << std::endl;
  }
  if (vm.count("rest_data")) {
    std::cout << "rest api request response configuration file "
              << vm["rest_data"].as<std::string>() << std::endl;
  }
  else {
    std::cout << "Rest api test data file was not set." << std::endl;
  }
  if (vm.count("dest")) {
      std::cout << "destination list: ";
      for (auto& dest : vm["dest"].as<std::vector<std::string> >())
          std::cout << dest << std::endl;
  }
  else {
    std::cout << "Destination list was not set." << std::endl;
  }

  if (vm.count("rest_data") == 0 || vm.count("data_dir") == 0 ||
      vm.count("port") == 0) {
    std::cout << "hint: mock_rest_api --port <port> --data_dir <directory> "
              <<"""--rest_data <rest_data>\t\tversion"
              << VERSION << "\n\n"
              << "\thttp_caching_proxy is a small http proxy that saves the\n"
              << "\tsuccssful requests passed to it to a json file for future "
              << "playback.\n"

              << "\tExample: http_caching_proxy --port 8181 "
              << "--data_dir /home/mock_rest_api --rest_data rest_data.json "
              << "--dest localhost:8080 --dest localhost:9090\n\n";

    std::cout << "\n\tNot Supported: URLs including \"..\", Java,Javascript, CGI\n"
              << "\tNot Supported: directories ";
    for (auto& bad_dir : BAD_DIRS)
      std::cout << bad_dir << " ";
    std::cout << std::endl;
    exit(0);
  }
  port = vm["port"].as<int>();
  data_dir = vm["data_dir"].as<std::string>();
  rest_data = vm["rest_data"].as<std::string>();
  if (vm.count("dest") > 0) {
    for (auto& dest : vm["dest"].as<std::vector<std::string> >()) {
      std::string host = dest;
      unsigned short destPort = 80;
      auto pos = dest.find(':');
      std::string portStr = "80";
      if (pos != dest.npos) {
        host = dest.substr(0,pos);
        portStr = dest.substr(pos+1);
        std::istringstream iss(portStr);
        iss >> destPort;
        if (destPort == 0) {
          std::cout << "Error parsing destination port: "
                    << portStr << " at " << pos << " of "
                    << dest << std::endl;
          exit(-1);
        }
        else if (destPort < 1 || destPort > 65535) {
          std::cerr << "Error: " << destPort << " is not between 1 and 65535."
                    << std::endl;
          exit(-2);
        }
      }
      destMap[dest] = connect(host, portStr);
    }
  }

  for (auto& bad_dir : BAD_DIRS) {
    if (bad_dir == data_dir) {
      std::cout << "ERROR: Bad top directory " << data_dir
                << ", see mock_rest_api -?" << std::endl;
      exit(3);
    }
  }

  if (chdir(data_dir.c_str()) == -1){
    std::cout << "ERROR: Can't Change to directory " << data_dir
              << std::endl;
    exit(4);
  }
  if (!init_rest_data(rest_data.c_str())) {
    std::cout << "ERROR: Can't load test data from " << data_dir << "/"
              << rest_data << std::endl;
    exit(5);
  }
}

int main(int argc, char **argv) {
  po::options_description desc("Allowed options");
  desc.add_options()
    ("data_dir",  po::value<std::string>(), "rest api response files")
    ("rest_data", po::value<std::string>(), "json based rest api data file")
    ("dest",      po::value<std::vector<std::string> >(), "list of comma separated host:port pairs")
    ("port",      po::value<int>(),         "tcp port")
    ("debug",                               "debug mode");
    ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  int port;
  std::string data_dir;
  std::string rest_data;
  std::map<std::string, int> dest_map;
  bool is_debug = false;
  parse_command_line(vm, port, data_dir, rest_data, dest_map, is_debug);
  int hit = 0;
  if (is_debug) {
    debug(port, hit, data_dir, dest_map);
  }
  else {
    return daemon(port, hit, data_dir, dest_map);
  }
}
