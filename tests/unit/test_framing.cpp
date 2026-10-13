// Slice 1: LineBuffer, MessageAssembler, and Connection (over socketpair).
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#include "common/connection.h"
#include "common/line_buffer.h"
#include "common/message_assembler.h"
#include "minitest.h"

namespace {

// Feeds the text one chunk at a time and collects every event.
std::vector<LineEvent> feed(LineBuffer& buffer, const std::vector<std::string>& chunks) {
    std::vector<LineEvent> events;
    for (const auto& chunk : chunks) {
        buffer.append(chunk.data(), chunk.size());
        LineEvent event;
        while (buffer.next(event)) events.push_back(event);
    }
    return events;
}

std::vector<std::string> one_byte_chunks(const std::string& text) {
    std::vector<std::string> chunks;
    for (char c : text) chunks.push_back(std::string(1, c));
    return chunks;
}

}  // namespace

TEST_CASE("T31 LineBuffer: one line split across two reads") {
    LineBuffer buffer;
    auto events = feed(buffer, {"CREATE_GAME Rob", "ert 3\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK(events[0].kind == LineEvent::Line);
    CHECK_EQ(events[0].text, std::string("CREATE_GAME Robert 3"));
}

TEST_CASE("T31 LineBuffer: partial line stays buffered until completed") {
    LineBuffer buffer;
    auto events = feed(buffer, {"LEADER"});
    CHECK(events.empty());
    events = feed(buffer, {"BOARD\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK_EQ(events[0].text, std::string("LEADERBOARD"));
}

TEST_CASE("T32 LineBuffer: two lines in one chunk come out in order") {
    LineBuffer buffer;
    auto events = feed(buffer, {"LEADERBOARD\nLEADERBOARD\nQUI"});
    CHECK_EQ(events.size(), static_cast<size_t>(2));
    CHECK_EQ(events[0].text, std::string("LEADERBOARD"));
    CHECK_EQ(events[1].text, std::string("LEADERBOARD"));
    events = feed(buffer, {"T\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK_EQ(events[0].text, std::string("QUIT"));
}

TEST_CASE("T33 LineBuffer: one byte at a time gives the same line") {
    LineBuffer buffer;
    auto events = feed(buffer, one_byte_chunks("CREATE_GAME Robert 3\n"));
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK_EQ(events[0].text, std::string("CREATE_GAME Robert 3"));
}

TEST_CASE("Slice1 LineBuffer: carriage return before newline is stripped") {
    LineBuffer buffer;
    auto events = feed(buffer, {"QUIT\r\n", "\r\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(2));
    CHECK_EQ(events[0].text, std::string("QUIT"));
    CHECK_EQ(events[1].text, std::string(""));
}

TEST_CASE("Slice1 LineBuffer: empty line is a line") {
    LineBuffer buffer;
    auto events = feed(buffer, {"\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK(events[0].kind == LineEvent::Line);
}

TEST_CASE("T21 LineBuffer: 512 bytes including newline is accepted") {
    LineBuffer buffer;
    const std::string body(511, 'x');
    auto events = feed(buffer, {body + "\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK(events[0].kind == LineEvent::Line);
    CHECK_EQ(events[0].text.size(), static_cast<size_t>(511));
}

TEST_CASE("T21 LineBuffer: 513 bytes including newline is over-length") {
    LineBuffer buffer;
    const std::string body(512, 'x');
    auto events = feed(buffer, {body + "\nLEADERBOARD\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(2));
    CHECK(events[0].kind == LineEvent::TooLong);
    CHECK(events[1].kind == LineEvent::Line);
    CHECK_EQ(events[1].text, std::string("LEADERBOARD"));
}

TEST_CASE("T21 LineBuffer: 600 characters with no newline reports once then recovers") {
    LineBuffer buffer;
    auto events = feed(buffer, {std::string(300, 'a'), std::string(300, 'b')});
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK(events[0].kind == LineEvent::TooLong);
    // More junk of the same line must not produce a second event.
    events = feed(buffer, {std::string(1000, 'c')});
    CHECK(events.empty());
    events = feed(buffer, {"tail\nLEADERBOARD\n"});
    CHECK_EQ(events.size(), static_cast<size_t>(1));
    CHECK_EQ(events[0].text, std::string("LEADERBOARD"));
}

TEST_CASE("T21 LineBuffer: over-length line arriving one byte at a time") {
    LineBuffer buffer;
    auto events = feed(buffer, one_byte_chunks(std::string(600, 'z') + "\nQUIT\n"));
    CHECK_EQ(events.size(), static_cast<size_t>(2));
    CHECK(events[0].kind == LineEvent::TooLong);
    CHECK_EQ(events[1].text, std::string("QUIT"));
}

TEST_CASE("Slice1 MessageAssembler: lines until blank line form one reply") {
    MessageAssembler assembler;
    Message message;
    CHECK(!assembler.add_line("200 OK", message));
    CHECK(!assembler.add_line("7QXA", message));
    CHECK(assembler.add_line("", message));
    CHECK(!message.is_push);
    CHECK_EQ(message.lines.size(), static_cast<size_t>(2));
    CHECK_EQ(message.lines[0], std::string("200 OK"));
}

TEST_CASE("Slice1 MessageAssembler: first line PUSH marks a push") {
    MessageAssembler assembler;
    Message message;
    CHECK(!assembler.add_line("PUSH QUESTION 1 What?", message));
    CHECK(!assembler.add_line("A) x", message));
    CHECK(assembler.add_line("", message));
    CHECK(message.is_push);
}

TEST_CASE("Slice1 MessageAssembler: a line starting PUSHY is not a push") {
    MessageAssembler assembler;
    Message message;
    assembler.add_line("PUSHY", message);
    CHECK(assembler.add_line("", message));
    CHECK(!message.is_push);
}

TEST_CASE("Slice1 MessageAssembler: two messages in a row come out separately") {
    MessageAssembler assembler;
    Message message;
    std::vector<Message> out;
    for (const char* line : {"200 OK", "", "PUSH GAME_END HOST_ENDED", "FINAL RESULTS", ""}) {
        if (assembler.add_line(line, message)) out.push_back(message);
    }
    CHECK_EQ(out.size(), static_cast<size_t>(2));
    CHECK(!out[0].is_push);
    CHECK(out[1].is_push);
    CHECK_EQ(out[1].lines.size(), static_cast<size_t>(2));
}

TEST_CASE("Slice1 MessageAssembler: stray blank line is ignored") {
    MessageAssembler assembler;
    Message message;
    CHECK(!assembler.add_line("", message));
    CHECK(!assembler.add_line("200 OK", message));
    CHECK(assembler.add_line("", message));
    CHECK_EQ(message.lines.size(), static_cast<size_t>(1));
}

namespace {
struct Pair {
    int a = -1, b = -1;
    Pair() {
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0) {
            a = fds[0];
            b = fds[1];
        }
    }
};
}  // namespace

TEST_CASE("Slice1 Connection: reads a line sent in pieces") {
    Pair pair;
    Connection reader(pair.a);
    Connection writer(pair.b);
    writer.send_raw("HEL");
    writer.send_raw("LO\nNEXT\n");
    std::string line;
    CHECK(reader.read_line(line) == Connection::ReadStatus::Line);
    CHECK_EQ(line, std::string("HELLO"));
    CHECK(reader.read_line(line) == Connection::ReadStatus::Line);
    CHECK_EQ(line, std::string("NEXT"));
}

TEST_CASE("Slice1 Connection: closed peer is reported as closed") {
    Pair pair;
    Connection reader(pair.a);
    { Connection writer(pair.b); }
    std::string line;
    CHECK(reader.read_line(line) == Connection::ReadStatus::Closed);
}

TEST_CASE("Slice1 Connection: partial line then close is closed, not a line") {
    Pair pair;
    Connection reader(pair.a);
    {
        Connection writer(pair.b);
        writer.send_raw("PARTIAL");
    }
    std::string line;
    CHECK(reader.read_line(line) == Connection::ReadStatus::Closed);
}

TEST_CASE("Slice1 Connection: read timeout when nothing arrives") {
    Pair pair;
    Connection reader(pair.a);
    Connection writer(pair.b);
    std::string line;
    CHECK(reader.read_line(line, 20) == Connection::ReadStatus::Timeout);
}

TEST_CASE("T21 Connection: over-length line reported then next command works") {
    Pair pair;
    Connection reader(pair.a);
    Connection writer(pair.b);
    writer.send_raw(std::string(600, 'x') + "\nLEADERBOARD\n");
    std::string line;
    CHECK(reader.read_line(line) == Connection::ReadStatus::TooLong);
    CHECK(reader.read_line(line) == Connection::ReadStatus::Line);
    CHECK_EQ(line, std::string("LEADERBOARD"));
}

TEST_CASE("Slice1 Connection: send_message adds the blank line") {
    Pair pair;
    Connection sender(pair.a);
    Connection receiver(pair.b);
    CHECK(sender.send_message({"200 OK", "7QXA"}));
    std::string line;
    receiver.read_line(line);
    CHECK_EQ(line, std::string("200 OK"));
    receiver.read_line(line);
    CHECK_EQ(line, std::string("7QXA"));
    receiver.read_line(line);
    CHECK_EQ(line, std::string(""));
}

TEST_CASE("Slice1 Connection: send_all handles a message bigger than the socket buffer") {
    Pair pair;
    Connection sender(pair.a);
    Connection receiver(pair.b);
    std::vector<std::string> lines(200, std::string(500, 'q'));
    std::atomic<bool> sent{false};
    std::thread writer([&] { sent = sender.send_message(lines); });
    int received = 0;
    std::string line;
    while (received < 201) {
        if (receiver.read_line(line) != Connection::ReadStatus::Line) break;
        ++received;
    }
    writer.join();
    CHECK(sent);
    CHECK_EQ(received, 201);
}

TEST_CASE("Slice1 Connection: sending to a closed peer fails without crashing") {
    Pair pair;
    Connection sender(pair.a);
    close(pair.b);
    bool all_ok = true;
    for (int i = 0; i < 5; ++i) all_ok = sender.send_message({"200 OK"}) && all_ok;
    CHECK(!all_ok);
    CHECK(sender.is_broken());
}

TEST_CASE("Slice1 Connection: send timeout makes a blocked write fail") {
    Pair pair;
    Connection sender(pair.a);
    Connection receiver(pair.b);  // never reads
    sender.set_send_timeout_ms(50);
    std::vector<std::string> lines(100, std::string(500, 'q'));
    bool failed = false;
    for (int i = 0; i < 200 && !failed; ++i) failed = !sender.send_message(lines);
    CHECK(failed);
    CHECK(sender.is_broken());
}

TEST_CASE("Slice1 Connection: two threads sending never interleave messages") {
    Pair pair;
    Connection sender(pair.a);
    Connection receiver(pair.b);
    const int per_thread = 300;
    auto spam = [&](char mark) {
        for (int i = 0; i < per_thread; ++i) {
            sender.send_message({std::string(1, mark) + "-first", std::string(200, mark)});
        }
    };
    std::thread t1(spam, 'A');
    std::thread t2(spam, 'B');
    MessageAssembler assembler;
    int messages = 0;
    bool clean = true;
    std::string line;
    Message message;
    while (messages < 2 * per_thread &&
           receiver.read_line(line) == Connection::ReadStatus::Line) {
        if (assembler.add_line(line, message)) {
            ++messages;
            const char mark = message.lines[0][0];
            clean = clean && message.lines.size() == 2 &&
                    message.lines[1] == std::string(200, mark);
        }
    }
    t1.join();
    t2.join();
    CHECK_EQ(messages, 2 * per_thread);
    CHECK(clean);
}
