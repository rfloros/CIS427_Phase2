#include "common/net_util.h"

#include <algorithm>
#include <cctype>

bool parse_port(const std::string& text, int& port) {
    if (text.empty() || text.size() > 5) return false;
    const bool all_digits = std::all_of(text.begin(), text.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
    if (!all_digits) return false;
    const int value = std::stoi(text);
    if (value < 1 || value > 65535) return false;
    port = value;
    return true;
}
