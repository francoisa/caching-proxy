#include "http_caching_proxy.h"
#include "server_main.h"

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <ctime>
#include <cerrno>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

const std::string VERSION = "1.0";
const int ERROR     =   42;
const int LOG       =   44;

static const int HEADER    =   45;

#define READ  0
#define WRITE 1

const std::array<std::string, 9> BAD_DIRS = {
   { "/", "/bin", "/etc", "lib", "/tmp", "/usr", "/var", "/opt", "/proc" } };

std::map<std::string, std::string> rest_data;

static bool is_debug = false;

void set_debug() {
  is_debug = true;
}

void logger(int type, const std::string& s1, const std::string& s2,
            int socket_fd, int hit) {
   std::ofstream logfile;
   if (!is_debug) {
     logfile.open("http_caching_proxy.log", std::ofstream::app);
   }
   std::ostream& log = is_debug ? std::cout : logfile;
   auto start = std::chrono::system_clock::now();
   std::time_t start_time = std::chrono::system_clock::to_time_t(start);
   std::ostringstream resp;
   char mbstr[100];
   if (std::strftime(mbstr, 100, "%c", std::localtime(&start_time))) {
     log << mbstr << ": ";
   }

   switch (type) {
   case ERROR: log << "ERROR: " << s1 << ": " << s2 << " Errno = "
                   << errno << " = " << std::strerror(errno) << std::endl;
     break;
   case LOG:
     log << "INFO: " << s1 << ": " << s2 << " Socket ID: " << socket_fd
         << " hit: " << hit << std::endl;
     break;
   case HEADER:
     log << s1 << ":\n" << s2 << "Socket ID: " << socket_fd << " hit: "
         << hit << std::endl;
   }
   log.flush();
   if (!is_debug) {
     logfile.close();
   }
}

void logger(int type, const std::string& s1, std::ostringstream& oss,
            int socket_fd, int hit) {
  logger(type, s1, oss.str(), socket_fd, hit);
  oss.str(std::string());
}

void proxy(int clntSock, int hit,
           const std::vector<std::pair<std::string, std::string> >& dests) {
  // Create separate memory for client argument
  ThreadArgs threadArgs;

  threadArgs.clntSock = clntSock;
  threadArgs.hit = hit;
  threadArgs.dests = dests;
  threadArgs.rest_data = rest_data;

  // Create client thread
  std::unique_ptr<ServerMain> sm{new ServerMain(threadArgs)};

  std::thread t(&ServerMain::proxy, sm.get());
  sm->up = std::move(sm);
  t.detach();

  logger(LOG, "proxy", "finished", clntSock, hit);
}
