// Slice 2: command parsing, error texts, and byte-exact message builders.
#include "minitest.h"
#include "common/protocol.h"

namespace {

ParseResult parse(const std::string& line, int available = 26) {
    return parse_command(line, available);
}

bool rejected(const std::string& line, int code, int available = 26) {
    const ParseResult result = parse(line, available);
    return !result.ok && result.error_code == code;
}

}  // namespace

TEST_CASE("Slice2 every command parses to its typed Command") {
    ParseResult r = parse("CREATE_GAME Robert 10");
    CHECK(r.ok && r.command.type == CommandType::CreateGame);
    CHECK_EQ(r.command.name, std::string("Robert"));
    CHECK_EQ(r.command.num_questions, 10);

    r = parse("JOIN_GAME 7qxa Matthew");
    CHECK(r.ok && r.command.type == CommandType::JoinGame);
    CHECK_EQ(r.command.code, std::string("7QXA"));  // codes are case-insensitive
    CHECK_EQ(r.command.name, std::string("Matthew"));

    r = parse("START_ROUND");
    CHECK(r.ok && r.command.type == CommandType::StartRound);

    r = parse("SUBMIT_ANSWER b");
    CHECK(r.ok && r.command.type == CommandType::SubmitAnswer);
    CHECK_EQ(r.command.choice, 'B');

    r = parse("LEADERBOARD");
    CHECK(r.ok && r.command.type == CommandType::Leaderboard);

    r = parse("RECONNECT Robert 7QXA K3m9");
    CHECK(r.ok && r.command.type == CommandType::Reconnect);
    CHECK_EQ(r.command.name, std::string("Robert"));
    CHECK_EQ(r.command.code, std::string("7QXA"));
    CHECK_EQ(r.command.token, std::string("K3m9"));  // tokens are case-sensitive

    r = parse("QUIT");
    CHECK(r.ok && r.command.type == CommandType::Quit);
    r = parse("END_GAME");
    CHECK(r.ok && r.command.type == CommandType::EndGame);
}

TEST_CASE("T14 empty line is a 300") { CHECK(rejected("", 300)); }

TEST_CASE("T15 unknown command is a 300") {
    CHECK(rejected("HELLO", 300));
    CHECK(rejected("create_game Robert 3", 300));  // command names are case-sensitive
    CHECK(rejected("CREATE_GAMES Robert 3", 300));
}

TEST_CASE("T16 bad CREATE_GAME arguments") {
    CHECK(rejected("CREATE_GAME Robert abc", 423));
    CHECK(rejected("CREATE_GAME Robert 0", 423));
    CHECK(rejected("CREATE_GAME Robert 9999", 423));
    CHECK(rejected("CREATE_GAME Robert -3", 423));
    CHECK(rejected("CREATE_GAME Robert 27", 423));
    CHECK(rejected("CREATE_GAME Robert 99999999999999999999", 423));
    CHECK(rejected("CREATE_GAME", 300));
    CHECK(parse("CREATE_GAME Robert 26").ok);
    CHECK(parse("CREATE_GAME Robert 1").ok);
}

TEST_CASE("Slice2 CREATE_GAME missing count after a valid name is 423") {
    CHECK(rejected("CREATE_GAME Robert", 423));
}

TEST_CASE("Slice2 CREATE_GAME extra arguments are 300") {
    CHECK(rejected("CREATE_GAME Robert 3 extra", 300));
    CHECK(rejected("CREATE_GAME Robert abc extra", 300));
}

TEST_CASE("Slice2 CREATE_GAME invalid name wins over invalid count") {
    CHECK(rejected("CREATE_GAME Rob:ert abc", 300));
    CHECK(rejected("CREATE_GAME ThisNameIsFarTooLongForTheGame 0", 300));
    CHECK(rejected("CREATE_GAME Rob:ert", 300));
}

TEST_CASE("Slice2 name rules") {
    CHECK(parse("CREATE_GAME A 1").ok);
    CHECK(parse("CREATE_GAME 12345678901234567890 1").ok);        // 20 characters
    CHECK(rejected("CREATE_GAME 123456789012345678901 1", 300));  // 21 characters
    CHECK(rejected("CREATE_GAME Rob:ert 1", 300));
    CHECK(rejected("CREATE_GAME R\xC3\xB6" "bert 1", 300));       // non-ASCII
    CHECK(rejected("CREATE_GAME Rob\tert 1", 300));
    CHECK(rejected("CREATE_GAME  1", 300));                       // empty name
    CHECK(rejected("CREATE_GAME Robert  3", 300));                // double space
    CHECK(rejected("CREATE_GAME Robert 3 ", 300));                // trailing space
}

TEST_CASE("Slice2 JOIN_GAME validation") {
    CHECK(rejected("JOIN_GAME", 300));
    CHECK(rejected("JOIN_GAME 7QXA", 300));
    CHECK(rejected("JOIN_GAME 7QXA Matthew extra", 300));
    CHECK(rejected("JOIN_GAME 7QX Matthew", 300));
    CHECK(rejected("JOIN_GAME 7QXAB Matthew", 300));
    CHECK(rejected("JOIN_GAME 7Q-A Matthew", 300));
    CHECK(rejected("JOIN_GAME 7QXA Matt:hew", 300));
    CHECK(parse("JOIN_GAME zzzz Matthew").ok);
}

TEST_CASE("T20 SUBMIT_ANSWER choice validation") {
    CHECK(rejected("SUBMIT_ANSWER E", 300));
    CHECK(rejected("SUBMIT_ANSWER", 300));
    CHECK(rejected("SUBMIT_ANSWER AB", 300));
    CHECK(rejected("SUBMIT_ANSWER A B", 300));
    CHECK(rejected("SUBMIT_ANSWER 1", 300));
    for (const char* choice : {"A", "B", "C", "D", "a", "b", "c", "d"}) {
        CHECK(parse(std::string("SUBMIT_ANSWER ") + choice).ok);
    }
}

TEST_CASE("Slice2 commands without arguments reject extras") {
    for (const char* name : {"START_ROUND", "LEADERBOARD", "QUIT", "END_GAME"}) {
        CHECK(rejected(std::string(name) + " now", 300));
        CHECK(rejected(std::string(name) + " ", 300));
    }
}

TEST_CASE("Slice2 RECONNECT malformed code or token is 300") {
    CHECK(rejected("RECONNECT Robert 7QXA", 300));
    CHECK(rejected("RECONNECT Robert 7QXA K3M9 extra", 300));
    CHECK(rejected("RECONNECT Robert 7QX K3M9", 300));
    CHECK(rejected("RECONNECT Robert 7QXA K3M", 300));
    CHECK(rejected("RECONNECT Robert 7QXA K3M99", 300));
    CHECK(rejected("RECONNECT Robert 7QXA K3-9", 300));
    CHECK(rejected("RECONNECT Rob:ert 7QXA K3M9", 300));
    CHECK(parse("RECONNECT Robert ZZZZ AAAA").ok);  // well formed, wrong later = 410
}

TEST_CASE("Slice2 control and non-ASCII bytes anywhere reject the line") {
    CHECK(rejected("LEADERBOARD\x01", 300));
    CHECK(rejected("QUIT\xFF", 300));
}

TEST_CASE("Slice2 status lines have the exact protocol text") {
    CHECK_EQ(status_line(200), std::string("200 OK"));
    CHECK_EQ(status_line(300), std::string("300 message format error"));
    CHECK_EQ(status_line(401),
             std::string("401 You are not currently in a game, join or reconnect first."));
    CHECK_EQ(status_line(402), std::string("402 Only the host may execute this command."));
    CHECK_EQ(status_line(404), std::string("404 No game exists with that code."));
    CHECK_EQ(status_line(409), std::string("409 That name is already in use in this game."));
    CHECK_EQ(status_line(410),
             std::string("410 Wrong name, game code, or token, or the reconnect window has "
                         "expired."));
    CHECK_EQ(status_line(420), std::string("420 There is no active round to answer."));
    CHECK_EQ(status_line(421), std::string("421 The game has already started."));
    CHECK_EQ(status_line(422),
             std::string("422 You have already submitted an answer for this round."));
    CHECK_EQ(status_line(423, 26),
             std::string("423 Requested question count must be between 1 and 26."));
}

TEST_CASE("Slice2 reply builders are byte exact") {
    CHECK(reply_ok() == Lines({"200 OK"}));
    CHECK(reply_error(410) == Lines({status_line(410)}));
    CHECK(reply_error(423, 5) == Lines({"423 Requested question count must be between 1 and 5."}));
    CHECK(reply_create("7QXA", "K3M9") == Lines({"200 OK", "7QXA", "K3M9"}));
    CHECK(reply_join(2, "7QXA", "P8L2") == Lines({"200 OK", "2 players in game 7QXA", "P8L2"}));
    CHECK(reply_leaderboard({{"Matthew", 3}, {"Robert", 2}}) ==
          Lines({"200 OK", "Matthew:3", "Robert:2"}));
    CHECK(reply_reconnect(2, 3) == Lines({"200 OK", "2 3"}));
}

TEST_CASE("Slice2 push builders are byte exact") {
    CHECK(push_question(3, "Which planet is known as the Red Planet?",
                        {"Venus", "Mars", "Jupiter", "Mercury"}) ==
          Lines({"PUSH QUESTION 3 Which planet is known as the Red Planet?", "A) Venus",
                 "B) Mars", "C) Jupiter", "D) Mercury"}));
    CHECK(push_round_result(3, 'B', {{"Matthew", 3}, {"Robert", 2}}) ==
          Lines({"PUSH ROUND_RESULT 3", "Correct answer: B", "Matthew:3", "Robert:2"}));
    CHECK(push_game_end(GameEndReason::Complete, {{"Matthew", 5}, {"Robert", 4}}) ==
          Lines({"PUSH GAME_END COMPLETE", "FINAL RESULTS", "Matthew:5", "Robert:4"}));
    CHECK(push_game_end(GameEndReason::HostEnded, {})[0] == "PUSH GAME_END HOST_ENDED");
    CHECK(push_game_end(GameEndReason::HostQuit, {})[0] == "PUSH GAME_END HOST_QUIT");
}

TEST_CASE("Slice2 order standings puts highest score first and keeps join order for ties") {
    const auto sorted = sort_standings({{"A", 1}, {"B", 3}, {"C", 1}, {"D", 3}});
    CHECK_EQ(sorted[0].name, std::string("B"));
    CHECK_EQ(sorted[1].name, std::string("D"));
    CHECK_EQ(sorted[2].name, std::string("A"));
    CHECK_EQ(sorted[3].name, std::string("C"));
}

TEST_CASE("Slice2 command_name gives the wire word") {
    CHECK_EQ(std::string(command_name(CommandType::Reconnect)), std::string("RECONNECT"));
    CHECK_EQ(std::string(command_name(CommandType::EndGame)), std::string("END_GAME"));
}
