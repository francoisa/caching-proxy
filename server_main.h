#ifndef SERVER_MAIN_H
#define SERVER_MAIN_H

#include "http_caching_proxy.h"
#include <netdb.h>

#include <string>
#include <map>
#include <vector>
#include <memory>

// Structure of arguments to pass to client thread
struct ThreadArgs {
  ThreadArgs() = default;
  ThreadArgs(const ThreadArgs& ta) = default;
  int clntSock; // Socket descriptor for client
  int hit;
  std::vector<std::pair<std::string, std::string>> dests;
  std::map<std::string, std::string> rest_data;
};

class ServerMain {
  private:
    ThreadArgs threadArgs;
    std::string request;
    std::string response;

  protected:
    bool recv_request(const std::string& mode, int source, int flags);

    bool send_request(const std::string& mode, int destination) const;

    void save_response(uint64_t hash) const;

    bool send_response(uint64_t hash);

    void handle_getpid(int fd) const;

    std::string parse_path(const char* buffer, int len, int offset = 4) const;

    int connect(const std::string& host, const std::string& port) const;

    void parse_headers(const char* buffer, std::map<const char*,
                       std::string>& header);

    Method parse_method(const char* buffer, int fd);

    bool get_response(const std::string& bufStr, int& code) const;

    bool forward_data(const std::string& mode, int source, int destination,
                      int flags, int& code, std::string& body) const;

    bool forward_response(int source, int destination, int& code);

    bool forward_request(int source, int destination, int flags);
  public:
    ServerMain(const ThreadArgs& ta);
    ~ServerMain();
    std::unique_ptr<ServerMain> up;

    void proxy();
    void handle();
};

#endif
