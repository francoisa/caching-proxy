#ifndef HTTP_CACHING_PROXY_H
#define HTTP_CACHING_PROXY_H

#include <string>
#include <array>
#include <map>
#include <iosfwd>

extern const std::string VERSION;

struct Extension {
   const char* ext;
   const std::string filetype;
};

extern const std::array<Extension, 10> extensions;

extern const std::array<std::string, 9> BAD_DIRS;

extern const int ERROR;
extern const int LOG;

void set_debug();
void logger(int type, const std::string& s1, const std::string& s2, int
            socket_fd);
void proxy(int fd, int hit, const std::map<std::string, int>& dests);

bool init_rest_data(const std::string& json_file);

#endif
