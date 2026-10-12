#include "common/connection.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>

Connection::Connection(int fd) : fd_(fd) {}

Connection::~Connection() {
    if (fd_ >= 0) close(fd_);
}

Connection::ReadStatus Connection::read_line(std::string& line, int timeout_ms) {
    while (true) {
        LineEvent event;
        if (receive_buffer_.next(event)) {
            if (event.kind == LineEvent::TooLong) return ReadStatus::TooLong;
            line = std::move(event.text);
            return ReadStatus::Line;
        }

        pollfd waiting{fd_, POLLIN, 0};
        const int ready = poll(&waiting, 1, timeout_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return ReadStatus::Error;
        }
        if (ready == 0) return ReadStatus::Timeout;

        char chunk[4096];
        const ssize_t received = recv(fd_, chunk, sizeof(chunk), 0);
        if (received == 0) return ReadStatus::Closed;
        if (received < 0) {
            if (errno == EINTR) continue;
            // A reset connection means the peer is gone, same as a close.
            if (errno == ECONNRESET) return ReadStatus::Closed;
            return ReadStatus::Error;
        }
        receive_buffer_.append(chunk, static_cast<size_t>(received));
    }
}

bool Connection::send_message(const std::vector<std::string>& lines) {
    std::string bytes;
    for (const auto& line : lines) bytes += line + "\n";
    bytes += "\n";
    return send_raw(bytes);
}

bool Connection::send_raw(const std::string& bytes) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return send_all_locked(bytes);
}

// A partial send is retried until everything is written. MSG_NOSIGNAL turns a
// closed peer into an error return instead of a SIGPIPE that kills the process.
bool Connection::send_all_locked(const std::string& bytes) {
    if (broken_) return false;
    size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t written =
            send(fd_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) continue;
            broken_ = true;
            return false;
        }
        sent += static_cast<size_t>(written);
    }
    return true;
}

void Connection::set_send_timeout_ms(int milliseconds) {
    timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void Connection::shutdown_write() { shutdown(fd_, SHUT_WR); }

void Connection::shutdown_both() { shutdown(fd_, SHUT_RDWR); }
