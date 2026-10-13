// Helpers shared by the integration tests: an in-process server on an
// ephemeral port and a raw-socket client that sees the exact bytes.
#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <set>
#include <sstream>
#include <thread>

#include "common/connection.h"
#include "common/message_assembler.h"
#include "server/logger.h"
#include "server/server.h"

namespace support {

inline std::vector<Question> make_bank(int count) {
    std::vector<Question> bank;
    for (int k = 0; k < count; ++k) {
        Question q;
        q.text = "Question " + std::to_string(k);
        q.options = {"alpha", "beta", "gamma", "delta"};
        q.correct = "ABCD"[k % 4];
        bank.push_back(q);
    }
    return bank;
}

// The pushed text ends with the question's index, which fixes its answer.
inline char correct_for(const Message& question) {
    const std::string& first = question.lines[0];
    return "ABCD"[std::stoi(first.substr(first.rfind(' ') + 1)) % 4];
}

inline Timings fast_timings() {
    Timings timings;
    timings.question = 0.6;
    timings.intermission = 0.2;
    timings.final_intermission = 0.3;
    timings.reconnect = 1.0;
    return timings;
}

inline double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

class TestServer {
public:
    explicit TestServer(Timings timings = fast_timings(), int bank_size = 6, uint64_t seed = 1234)
        : logger_(log_stream_) {
        ServerOptions options;
        options.questions = make_bank(bank_size);
        options.timings = timings;
        options.seed = seed;
        options.logger = &logger_;
        options.port = 0;
        server_.reset(new Server(options));
        std::string error;
        started_ = server_->start(error);
        if (started_) thread_ = std::thread([this] { server_->run(); });
    }
    ~TestServer() { stop(); }

    bool started() const { return started_; }
    int port() const { return server_->port(); }
    Server& server() { return *server_; }

    void stop() {
        if (stopped_) return;
        stopped_ = true;
        if (started_) {
            server_->request_stop();
            thread_.join();
        }
    }
    std::string log_text() {
        stop();
        return log_stream_.str();
    }

private:
    std::ostringstream log_stream_;
    Logger logger_;
    std::unique_ptr<Server> server_;
    std::thread thread_;
    bool started_ = false;
    bool stopped_ = false;
};

class RawClient {
public:
    enum class Read { Message, Closed, Timeout };

    explicit RawClient(int port) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(fd);
            return;
        }
        connection_.reset(new Connection(fd));
        connected_ = true;
    }

    bool connected() const { return connected_; }
    void send(const std::string& bytes) { connection_->send_raw(bytes); }
    void send_line(const std::string& line) { send(line + "\n"); }

    Read read_message(Message& message, int timeout_ms = 3000) {
        const auto start = std::chrono::steady_clock::now();
        while (true) {
            const double left = timeout_ms - seconds_since(start) * 1000;
            if (left <= 0) return Read::Timeout;
            std::string line;
            const auto status = connection_->read_line(line, static_cast<int>(left));
            if (status == Connection::ReadStatus::Timeout) return Read::Timeout;
            if (status != Connection::ReadStatus::Line) return Read::Closed;
            if (assembler_.add_line(line, message)) return Read::Message;
        }
    }

    // Reads the next message; empty lines vector means timeout or close.
    Message next(int timeout_ms = 3000) {
        Message message;
        if (read_message(message, timeout_ms) != Read::Message) return Message{};
        return message;
    }

    // Sends a command and returns its reply, collecting pushes that came first.
    Message request(const std::string& line, std::vector<Message>* pushes = nullptr,
                    int timeout_ms = 3000) {
        send_line(line);
        while (true) {
            Message message = next(timeout_ms);
            if (message.lines.empty()) return message;
            if (!message.is_push) return message;
            if (pushes != nullptr) pushes->push_back(message);
        }
    }

    // True once the server closed the connection (reads until EOF, ignoring data).
    bool wait_for_close(int timeout_ms = 3000) {
        const auto start = std::chrono::steady_clock::now();
        while (seconds_since(start) * 1000 < timeout_ms) {
            std::string line;
            const auto status = connection_->read_line(line, 50);
            if (status == Connection::ReadStatus::Closed || status == Connection::ReadStatus::Error) {
                return true;
            }
        }
        return false;
    }

    // Drops the socket abruptly, like a killed client process.
    void kill() { connection_.reset(); }

    // Raw bytes until EOF (used to check exact byte sequences).
    std::string read_all_until_close(int timeout_ms = 3000) {
        std::string everything;
        const auto start = std::chrono::steady_clock::now();
        while (seconds_since(start) * 1000 < timeout_ms) {
            std::string line;
            const auto status = connection_->read_line(line, 50);
            if (status == Connection::ReadStatus::Line) everything += line + "\n";
            if (status == Connection::ReadStatus::Closed || status == Connection::ReadStatus::Error) {
                break;
            }
        }
        return everything;
    }

private:
    std::unique_ptr<Connection> connection_;
    MessageAssembler assembler_;
    bool connected_ = false;
};

struct Created {
    std::string code, token;
};

inline Created create_game(RawClient& client, const std::string& name, int questions,
                           std::vector<Message>* pushes = nullptr) {
    const Message reply =
        client.request("CREATE_GAME " + name + " " + std::to_string(questions), pushes);
    if (reply.lines.size() != 3) return Created{};
    return Created{reply.lines[1], reply.lines[2]};
}

}  // namespace support

#endif
