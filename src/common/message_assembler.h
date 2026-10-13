// Groups lines into whole server messages: a message is every line up to the
// next empty line (protocol 2.2). Pure logic, no sockets.
#ifndef COMMON_MESSAGE_ASSEMBLER_H
#define COMMON_MESSAGE_ASSEMBLER_H

#include <string>
#include <vector>

struct Message {
    std::vector<std::string> lines;  // without the blank terminator
    bool is_push = false;            // first line starts with the word PUSH
};

class MessageAssembler {
public:
    // Returns true and fills `message` when `line` was the blank terminator
    // of a complete message.
    bool add_line(const std::string& line, Message& message);

private:
    std::vector<std::string> pending_;
};

#endif
