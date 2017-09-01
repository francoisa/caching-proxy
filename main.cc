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

int listenfd = -1;

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

static sockaddr_in cli_addr; /* static = initialised to zeros */

int daemon(int port, int& hit, const std::string& data_dir,
           const std::vector<std::pair<std::string, std::string> >& dests) {
  logger(LOG, "starting", "become daemon", getpid());
  /* Become deamon + unstopable and no zombies children ( = no wait()) */
  if (fork() != 0)
    return 0; /* parent returns OK to shell */
  signal(SIGCLD, SIG_IGN); /* ignore child death */
  signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
  signal(SIGTERM, terminate);
  signal(SIGINT, terminate);
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
      proxy(socketfd, hit, dests); /* never returns */
    }
  }
}

void debug(int port, const std::string& data_dir,
           const std::vector<std::pair<std::string, std::string> >& dests) {
  set_debug();
  signal(SIGTERM, terminate);
  std::ostringstream portStr;
  portStr << port;
  logger(LOG, "listen on port", portStr.str().c_str(), getpid());
  /* setup the network socket */
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) <0)
    logger(ERROR, "debug", "socket creation", 0);
  if (port < 0 || port >60000)
    logger(ERROR, "debug", "Invalid port number (try 1->60000)", 0);
  static sockaddr_in serv_addr; /* static = initialised to zeros */
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  const sockaddr* servAddr = reinterpret_cast<const sockaddr*>(&serv_addr);
  if (bind(listenfd, servAddr, sizeof(serv_addr)) <0)
    logger(ERROR, "debug", "bind", 0);
  if (listen(listenfd, 64) < 0)
    logger(ERROR, "debug", "listen", 0);
  for (int hit = 1; true ;hit++) {
    int socketfd;
    socklen_t length = sizeof(cli_addr);
    if ((socketfd = accept(listenfd,
                           (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "debug", "accept", hit);
    }
    else {
      proxy(socketfd, hit, dests); /* never returns */
    }
  }
}

void parse_command_line(const po::variables_map& vm, int& port, 
                        std::string& data_dir,
                        std::vector<std::pair<std::string, std::string> >& dests, 
                        bool& is_debug) {
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
  if (vm.count("dest")) {
      std::cout << "destination list: ";
      for (auto& dest : vm["dest"].as<std::vector<std::string> >())
          std::cout << dest << std::endl;
  }
  else {
    std::cout << "Destination list was not set." << std::endl;
  }

  if (vm.count("data_dir") == 0 ||
      vm.count("port") == 0) {
    std::cout << "hint: mock_rest_api --port <port> --data_dir <directory> "
              <<"""--version"
              << VERSION << "\n\n"
              << "\thttp_caching_proxy is a small http proxy that saves the\n"
              << "\tsuccssful requests passed to it to a json file for future "
              << "playback.\n"

              << "\tExample: http_caching_proxy --port 8181 "
              << "--data_dir /home/mock_rest_api "
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
      dests.push_back(std::make_pair(host, portStr));
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
}

int main(int argc, char **argv) {
  po::options_description desc("Allowed options");
  desc.add_options()
    ("data_dir",  po::value<std::string>(), "rest api response files")
    ("dest",      po::value<std::vector<std::string> >(), "list of comma separated host:port pairs")
    ("port",      po::value<int>(),         "tcp port")
    ("debug",                               "debug mode");
    ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  int port;
  std::string data_dir;
  std::vector<std::pair<std::string, std::string> > dests;
  bool is_debug = false;
  parse_command_line(vm, port, data_dir, dests, is_debug);
  int hit = 0;
  if (is_debug) {
    debug(port, data_dir, dests);
  }
  else {
    return daemon(port, hit, data_dir, dests);
  }
}
