// Cuts a stream of received bytes into lines. Knows nothing about sockets,
// so it can be tested by feeding it strings.
#ifndef COMMON_LINE_BUFFER_H
#define COMMON_LINE_BUFFER_H

#include <cstddef>
#include <string>

struct LineEvent {
    enum Kind { Line, TooLong };
    Kind kind = Line;
    std::string text;  // the line without its \n or \r\n; empty for TooLong
};

class LineBuffer {
public:
    // A line may be at most 512 bytes including its newline (protocol 6.1).
    static constexpr size_t kMaxLineBytes = 512;

    void append(const char* data, size_t size);

    // Returns true and fills `event` when a complete line (or an over-length
    // report) is ready. Call it in a loop after every append.
    bool next(LineEvent& event);

private:
    std::string buffer_;
    // True after an over-length line was reported but its newline has not
    // arrived yet; everything up to that newline is thrown away.
    bool discarding_ = false;
};

#endif
