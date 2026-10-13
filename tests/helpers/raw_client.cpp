// A scripted raw-socket client for the shell tests and the live demo.
//
//   raw_client <host> <port> [--stamp] STEP...
//
// Steps run in order:
//   send TEXT          send TEXT as is (\n, \r, \\ are understood)
//   sendrep CHAR N     send CHAR repeated N times
//   bytewise TEXT MS   send TEXT one byte at a time, MS milliseconds apart
//   sleep MS           pause (anything that arrives meanwhile is printed)
//   read MS            print what arrives for MS milliseconds (stops early at EOF)
//   closewait MS       wait up to MS milliseconds for the server to close
//   kill               drop the connection without a goodbye and stop
//
// Received bytes go to standard output. With --stamp each line is prefixed
// with the milliseconds since the program started, and each `send` step is
// announced with a ">> sent" line so latencies can be computed.
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::chrono::steady_clock::time_point program_start = std::chrono::steady_clock::now();
bool stamp_lines = false;
std::string pending_line;
bool server_closed = false;

long elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - program_start)
        .count();
}

std::string unescape(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char next = text[++i];
            out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
        } else {
            out += text[i];
        }
    }
    return out;
}

void print_received(const char* data, size_t size) {
    if (!stamp_lines) {
        std::cout.write(data, static_cast<std::streamsize>(size)).flush();
        return;
    }
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\n') {
            std::cout << "[+" << elapsed_ms() << "ms] " << pending_line << "\n" << std::flush;
            pending_line.clear();
        } else {
            pending_line += data[i];
        }
    }
}

// Waits up to `ms` for data; returns false once the server has closed.
void read_for(int fd, long ms, bool stop_at_eof) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!server_closed) {
        const long left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              end - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0) return;
        pollfd waiting{fd, POLLIN, 0};
        if (poll(&waiting, 1, static_cast<int>(left)) <= 0) return;
        char chunk[4096];
        const ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0) {
            server_closed = true;
            return;
        }
        print_received(chunk, static_cast<size_t>(got));
    }
    (void)stop_at_eof;
}

void send_all(int fd, const std::string& bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t written = send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (written <= 0) return;
        sent += static_cast<size_t>(written);
    }
}

int connect_to(const std::string& host, const std::string& port) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0) return -1;
    int fd = -1;
    for (addrinfo* a = found; a != nullptr; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(found);
    return fd;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: raw_client <host> <port> [--stamp] STEP...\n";
        return 2;
    }
    std::vector<std::string> args(argv + 3, argv + argc);
    if (!args.empty() && args[0] == "--stamp") {
        stamp_lines = true;
        args.erase(args.begin());
    }

    const int fd = connect_to(argv[1], argv[2]);
    if (fd < 0) {
        std::cerr << "raw_client: could not connect\n";
        return 1;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& step = args[i];
        if (step == "send" && i + 1 < args.size()) {
            if (stamp_lines) std::cout << "[+" << elapsed_ms() << "ms] >> sent " << args[i + 1] << "\n" << std::flush;
            send_all(fd, unescape(args[++i]));
        } else if (step == "sendrep" && i + 2 < args.size()) {
            send_all(fd, std::string(static_cast<size_t>(std::atol(args[i + 2].c_str())),
                                     args[i + 1].empty() ? 'x' : args[i + 1][0]));
            i += 2;
        } else if (step == "bytewise" && i + 2 < args.size()) {
            const std::string text = unescape(args[i + 1]);
            const long delay = std::atol(args[i + 2].c_str());
            for (char c : text) {
                send_all(fd, std::string(1, c));
                read_for(fd, delay, false);
            }
            i += 2;
        } else if (step == "sleep" && i + 1 < args.size()) {
            read_for(fd, std::atol(args[++i].c_str()), false);  // keeps reading so stamps are accurate
        } else if ((step == "read" || step == "closewait") && i + 1 < args.size()) {
            read_for(fd, std::atol(args[++i].c_str()), step == "closewait");
        } else if (step == "kill") {
            break;  // closing the descriptor below ends the connection
        } else {
            std::cerr << "raw_client: bad step: " << step << "\n";
            return 2;
        }
    }
    close(fd);
    return 0;
}
