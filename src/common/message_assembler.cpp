#include "common/message_assembler.h"

namespace {

bool starts_with_push_word(const std::string& line) {
    return line == "PUSH" || line.compare(0, 5, "PUSH ") == 0;
}

}  // namespace

bool MessageAssembler::add_line(const std::string& line, Message& message) {
    if (!line.empty()) {
        pending_.push_back(line);
        return false;
    }
    if (pending_.empty()) return false;  // a stray blank line is not a message

    message.lines = std::move(pending_);
    message.is_push = starts_with_push_word(message.lines[0]);
    pending_.clear();
    return true;
}
