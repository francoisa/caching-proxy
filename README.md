# http-caching-proxy
An stl C++11 and POSIX sockets based caching proxy
The idea here is that you specify one or more destination http address/port pairs (if no port is specified it is assumed to be port 80).

THe server listens on the specified local port and proxies your requests to the destination(s). If any of the destinations responds successfully to the request the path and response are saved.  The results can be played back without the need for the destination over time.

* Uses boost program options for parsing the command line
* Uses boost property tree for parsing and creating the json file with request/response data
* Uses the C++11 to act as a mutithreaded proxy
* On;y supports GET method
* kill 15 <pid>: kills the server
* http://localhost:<port>/getpid returns the pid of the daemon.
