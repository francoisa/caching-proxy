#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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

int main(int argc, char **argv) {
  po::options_description desc("Allowed options");
  desc.add_options()
    ("data_dir",  po::value<std::string>(), "rest api response files")
    ("rest_data", po::value<std::string>(), "json based rest api data file")
    ("dest",      po::value<std::vector<std::string> >(), "list of comma separated host:port pairs")
    ("port",      po::value<int>(),         "tcp port")
    ;
  
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);    

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
    std::cout << "destination list: " 
              << vm["rest_data"].as<std::string>() << std::endl;
  }
  else {
    std::cout << "Destination list was not set." << std::endl;
  }
  
  int i, socketfd, hit;
  socklen_t length;
  static struct sockaddr_in cli_addr; /* static = initialised to zeros */
  static struct sockaddr_in serv_addr; /* static = initialised to zeros */
  std::multimap<std::string, int> destMap;
  
  if (vm.count("rest_data") == 0 || vm.count("data_dir") == 0 ||
      vm.count("port") == 0) {
    std::cout << "hint: mock_rest_api --port <port> --data_dir <directory> --rest_data <rest_data>\t\tversion"
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
  int port = vm["port"].as<int>();
  const auto data_dir = vm["data_dir"].as<std::string>();
  const auto rest_data = vm["rest_data"].as<std::string>();
  if (vm.count("dest") > 0) {
    for (auto& dest : vm["dest"].as<std::vector<std::string> >()) {
      std::string host = dest;
      unsigned short port = 80;  
      auto pos = dest.find(':');
      if (pos != dest.npos) {
        host = dest.substr(0,pos);
        std::istringstream iss(dest.substr(pos+1));
        iss >> port;
        if (port == 0) {
          std::cout << "Error parsing destination port: "
                    << dest.substr(pos+1)
                    << " at " << pos << " of "
                    << dest << std::endl;
          exit(-1);
        }
      }
      std::cout << host << ":" << port << std::endl;
      destMap.insert(std::pair<std::string, int>(host, port));
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
    std::cout << "ERROR: Can't load test data from rest_data.json "
              << std::endl;
    exit(5);
  }
  logger(LOG, "starting", "become daemon", getpid());
  /* Become deamon + unstopable and no zombies children ( = no wait()) */
  if (fork() != 0)
    return 0; /* parent returns OK to shell */
  signal(SIGCLD, SIG_IGN); /* ignore child death */
  signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
  signal(SIGTERM, terminate);
  logger(LOG, "starting", "close open files", getpid());
  for (i = 0; i < 32; i++)
    close(i); /* close open files */
  setpgrp(); /* break away from process group */
  std::ostringstream portStr;
  portStr << port;
  logger(LOG, "listen on port", portStr.str().c_str(), getpid());
  /* setup the network socket */
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) <0)
    logger(ERROR, "system call", "socket", 0);
  if (port < 0 || port >60000)
    logger(ERROR, "Invalid port number (try 1->60000)",
           vm["port"].as<std::string>(), 0);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) <0)
    logger(ERROR, "system call", "bind", 0);
  if ( listen(listenfd, 64) < 0)
    logger(ERROR, "system call", "listen", 0);
  for (hit = 1; true ;hit++) {
    length = sizeof(cli_addr);
    if ((socketfd = accept(listenfd,
                           (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "system call", "accept", 0);
    }
    else {
      proxy(socketfd, hit, destMap); /* never returns */
    }
  }
}
