// Small helpers shared by the client and server command-line handling.
#ifndef COMMON_NET_UTIL_H
#define COMMON_NET_UTIL_H

#include <string>

// Accepts only a whole decimal number from 1 to 65535.
bool parse_port(const std::string& text, int& port);

#endif
