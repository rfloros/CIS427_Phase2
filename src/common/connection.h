// One TCP connection: owns the socket, a receive buffer, and a write lock.
// Used by both the client and the server.
#ifndef COMMON_CONNECTION_H
#define COMMON_CONNECTION_H

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "common/line_buffer.h"

class Connection {
public:
    enum class ReadStatus { Line, TooLong, Closed, Error, Timeout };

    // Takes ownership of `fd` and closes it in the destructor.
    explicit Connection(int fd);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Blocks until a full line, an over-length report, a close, an error, or
    // `timeout_ms` passes (-1 waits forever). Only one thread may read.
    ReadStatus read_line(std::string& line, int timeout_ms = -1);

    // Sends the lines plus the blank terminator as one buffer under the write
    // lock, so two messages are never mixed. False means the send failed.
    bool send_message(const std::vector<std::string>& lines);
    // Sends exactly these bytes (used for client commands and by tests).
    bool send_raw(const std::string& bytes);

    // Bounds how long one blocked write may wait.
    void set_send_timeout_ms(int milliseconds);
    // Tells the peer no more data is coming but still lets it finish reading.
    void shutdown_write();
    // Wakes a thread blocked in read_line from another thread.
    void shutdown_both();

    int fd() const { return fd_; }

    // Flags set by other threads and read by the owner of the connection.
    void mark_closing() { closing_ = true; }
    bool is_closing() const { return closing_; }
    bool is_broken() const { return broken_; }

private:
    bool send_all_locked(const std::string& bytes);

    int fd_;
    LineBuffer receive_buffer_;
    std::mutex write_mutex_;
    std::atomic<bool> closing_{false};
    std::atomic<bool> broken_{false};
};

#endif
