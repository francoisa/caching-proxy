#include "http_caching_proxy.h"
#include "server_main.h"

#include <memory>
#include <thread>
#include <chrono>
#include <ctime>
#include <cerrno>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>

const std::string VERSION = "1.0";
const int ERROR     =   42;
const int LOG       =   44;

static const int HEADER    =   45;
static const int FORBIDDEN =  403;
static const int NOTFOUND  =  404;

#define READ  0
#define WRITE 1

const std::array<Extension, 10> extensions = { {
   {"gif", "image/gif" },
   {"jpg", "image/jpg" },
   {"jpeg", "image/jpeg"},
   {"png", "image/png" },
   {"ico", "image/ico" },
   {"zip", "image/zip" },
   {"gz", "image/gz"  },
   {"tar", "image/tar" },
   {"htm", "text/html" },
   {"html", "text/html" } } };

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
   case FORBIDDEN:
     resp << "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\n"
          << "Content-Type: text/html\n\n<html><head>\n"
          << "<title>403 Forbidden</title>\n</head><body>\n"
          << "<h1>Forbidden</h1>\n"
          << "The requested URL, file type or operation is not allowed on "
          << "this simple static file webserver.\n</body></html>\n";
     write(socket_fd, resp.str().c_str(), resp.str().size());
     log << "FORBIDDEN: " << s1 << ": " << s2<< socket_fd 
         << " hit: " << hit << std::endl;
     break;
   case NOTFOUND:
     resp << "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\n"
          << "Content-Type: text/html\n\n<html><head>\n"
          << "<title>404 Not Found</title>\n</head><body>\n"
          << "<h1>Not Found</h1>\nThe requested URL was not found on this"
          << " server.\n</body></html>\n";
     write(socket_fd, resp.str().c_str(), resp.str().size());
     log << "NOT FOUND: " << s1 << ": " << s2 << " Socket ID: " << socket_fd
         << " hit: " << hit << std::endl;
     break;
   case LOG:
     log << "INFO: " << s1 << ": " << s2 << " Socket ID: " << socket_fd 
         << " hit: " << hit << std::endl;
     break;
   case HEADER:
     log << s1 << ":\n" << s2 << "Socket ID: " << socket_fd << hit << std::endl;
   }
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

  if (dests.empty()) {
    std::thread t(&ServerMain::handle, sm.get());
    sm->up = std::move(sm);
    t.detach();
  }
  else {
    std::thread t(&ServerMain::proxy, sm.get());
    sm->up = std::move(sm);
    t.detach();
  }
  
  logger(LOG, "proxy", "finished", clntSock, hit);
}

bool init_rest_data(const std::string& json_file) {
   boost::property_tree::ptree ptree;
   std::ifstream ifs(json_file);
   if (ifs) {
     try {
       boost::property_tree::read_json(ifs, ptree);
     }
     catch (std::exception ex) {
       std::cerr << ex.what() << std::endl;
       exit(-1);
     }
   }
   else {
     std::cerr << json_file << " not found." << std::endl;
     exit(-1);
   }
   const auto& requests = ptree.get_child("requests");
   for (auto& field: requests) {
     const auto& path = field.second.get_optional<std::string>("path");
     const auto& response = field.second.get_optional<std::string>("response");
     if (path && response) {
       std::cout << "path=" << *path << " | response=" << *response << std::endl;
       if (rest_data.find(*response) == rest_data.end()) {
         rest_data[*path] = *response;
       }
       else {
         std::cerr << "Duplicate path '" << *path << "' found in "
                   << json_file << std::endl;
         exit(-3);
       }
     }
   }
   ifs.close();
   return true;
}
