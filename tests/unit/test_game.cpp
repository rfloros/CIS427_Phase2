// Slice 3: game logic with a fake clock and injected seed. No sockets, no sleeping.
#include <set>

#include "minitest.h"
#include "server/game.h"
#include "common/protocol.h"

namespace {

const Timings kTimings{20, 5, 10, 60};

// Question k has the text "Question k" and the correct letter ABCD[k % 4],
// so a test can work out the right answer from the pushed text alone.
std::vector<Question> make_bank(int count) {
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

char correct_for(const Lines& question_push) {
    const std::string& first = question_push[0];
    const int k = std::stoi(first.substr(first.rfind(' ') + 1));
    return "ABCD"[k % 4];
}

char wrong_for(const Lines& question_push) {
    return correct_for(question_push) == 'A' ? 'B' : 'A';
}

struct World {
    GameManager manager;
    double now = 1000;
    std::vector<std::string> all_events;

    explicit World(int bank_size = 6, uint64_t seed = 7, Timings timings = kTimings)
        : manager(make_bank(bank_size), timings, seed) {}

    Outcome run(ConnId conn, const std::string& line) {
        const ParseResult parsed = parse_command(line, manager.available_questions());
        Outcome outcome;
        if (!parsed.ok) {
            outcome.code = parsed.error_code;
            outcome.effects.push_back(
                {conn, reply_error(parsed.error_code, manager.available_questions()), false});
        } else {
            outcome = manager.handle(conn, parsed.command, now);
        }
        keep(outcome);
        return outcome;
    }

    Outcome tick_at(double seconds_from_now) {
        now += seconds_from_now;
        Outcome outcome = manager.tick(now);
        keep(outcome);
        return outcome;
    }

    void keep(const Outcome& outcome) {
        all_events.insert(all_events.end(), outcome.events.begin(), outcome.events.end());
    }
};

struct Session {
    std::string code, token;
};

// First effect addressed to `conn` (or the n-th with `index`).
const Effect* effect_for(const Outcome& outcome, ConnId conn, size_t index = 0) {
    size_t seen = 0;
    for (const auto& effect : outcome.effects) {
        if (effect.conn == conn && seen++ == index) return &effect;
    }
    return nullptr;
}

size_t count_for(const Outcome& outcome, ConnId conn) {
    size_t count = 0;
    for (const auto& effect : outcome.effects) count += effect.conn == conn;
    return count;
}

bool is_push(const Lines& lines, const std::string& kind) {
    return !lines.empty() && lines[0].rfind("PUSH " + kind, 0) == 0;
}

Session create(World& world, ConnId conn, const std::string& name, int questions) {
    const Outcome outcome = world.run(conn, "CREATE_GAME " + name + " " + std::to_string(questions));
    const Effect* reply = effect_for(outcome, conn);
    if (reply == nullptr || reply->lines.size() != 3) return Session{};
    return Session{reply->lines[1], reply->lines[2]};
}

Session join(World& world, ConnId conn, const std::string& code, const std::string& name) {
    const Outcome outcome = world.run(conn, "JOIN_GAME " + code + " " + name);
    const Effect* reply = effect_for(outcome, conn);
    if (reply == nullptr || reply->lines.size() != 3) return Session{};
    return Session{code, reply->lines[2]};
}

// Starts the game and returns the first QUESTION push.
Lines start(World& world, ConnId host) {
    const Outcome outcome = world.run(host, "START_ROUND");
    const Effect* push = effect_for(outcome, host, 1);
    return push != nullptr ? push->lines : Lines{};
}

// Ticks until the next QUESTION push for `conn` shows up (at most `limit` seconds).
Lines tick_to_question(World& world, ConnId conn, double limit = 40) {
    for (double waited = 0; waited <= limit; waited += 1) {
        const Outcome outcome = world.tick_at(1);
        for (const auto& effect : outcome.effects) {
            if (effect.conn == conn && is_push(effect.lines, "QUESTION")) return effect.lines;
        }
    }
    return {};
}

bool all_alnum(const std::string& text) {
    for (char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("T01 create game returns code and token and registers the host") {
    World world;
    const Outcome outcome = world.run(1, "CREATE_GAME Robert 3");
    CHECK_EQ(outcome.code, 200);
    const Effect* reply = effect_for(outcome, 1);
    CHECK(reply != nullptr);
    CHECK_EQ(reply->lines.size(), static_cast<size_t>(3));
    CHECK_EQ(reply->lines[0], std::string("200 OK"));
    CHECK_EQ(reply->lines[1].size(), static_cast<size_t>(4));
    CHECK_EQ(reply->lines[2].size(), static_cast<size_t>(4));
    CHECK(all_alnum(reply->lines[1]) && all_alnum(reply->lines[2]));
    CHECK_EQ(reply->lines[1], [&] {
        std::string upper = reply->lines[1];
        for (char& c : upper) c = static_cast<char>(toupper(c));
        return upper;
    }());
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(1));
    CHECK(world.manager.is_joined(1));
    CHECK(*world.manager.game_state(reply->lines[1]) == GameState::Lobby);
    CHECK(*world.manager.player_state(reply->lines[1], "Robert") == PlayerState::Active);
    CHECK_EQ(*world.manager.player_score(reply->lines[1], "Robert"), 0);
}

TEST_CASE("Slice3 join codes are unique among active games and tokens are 4 characters") {
    World world;
    std::set<std::string> codes;
    for (ConnId conn = 1; conn <= 300; ++conn) {
        const Session s = create(world, conn, "P" + std::to_string(conn), 1);
        CHECK_EQ(s.token.size(), static_cast<size_t>(4));
        codes.insert(s.code);
    }
    CHECK_EQ(codes.size(), static_cast<size_t>(300));
}

TEST_CASE("Slice3 same seed gives the same code, token, and question order") {
    World a(6, 99);
    World b(6, 99);
    World c(6, 100);
    const Session sa = create(a, 1, "Robert", 6);
    const Session sb = create(b, 1, "Robert", 6);
    const Session sc = create(c, 1, "Robert", 6);
    CHECK_EQ(sa.code, sb.code);
    CHECK_EQ(sa.token, sb.token);
    CHECK(sa.code != sc.code || sa.token != sc.token);
    CHECK(start(a, 1) == start(b, 1));
}

TEST_CASE("Slice3 questions are random without repeats across a whole game") {
    World world(6, 5);
    create(world, 1, "Robert", 6);
    std::set<std::string> seen;
    Lines question = start(world, 1);
    for (int i = 0; i < 6; ++i) {
        CHECK(!question.empty());
        seen.insert(question[0].substr(question[0].find(' ', 14) + 1));
        world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
        if (i < 5) question = tick_to_question(world, 1);
    }
    CHECK_EQ(seen.size(), static_cast<size_t>(6));
}

TEST_CASE("Slice3 CREATE_GAME above the bank size is a 423 and creates nothing") {
    World world(6);
    const Outcome outcome = world.run(1, "CREATE_GAME Robert 7");
    CHECK_EQ(outcome.code, 423);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(0));
}

TEST_CASE("T02 START_ROUND: reply first, then the question to the host") {
    World world;
    create(world, 1, "Robert", 3);
    const Outcome outcome = world.run(1, "START_ROUND");
    CHECK_EQ(outcome.effects.size(), static_cast<size_t>(2));
    CHECK(outcome.effects[0].lines == reply_ok());
    CHECK(is_push(outcome.effects[1].lines, "QUESTION 1 "));
    CHECK_EQ(outcome.effects[1].lines.size(), static_cast<size_t>(5));
}

TEST_CASE("T03 submit answer closes the round early with the result") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Lines question = start(world, 1);
    const Outcome outcome = world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    CHECK_EQ(outcome.effects.size(), static_cast<size_t>(2));
    CHECK(outcome.effects[0].lines == reply_ok());
    CHECK(is_push(outcome.effects[1].lines, "ROUND_RESULT 1"));
    CHECK_EQ(outcome.effects[1].lines[1], std::string("Correct answer: ") + correct_for(question));
    CHECK_EQ(outcome.effects[1].lines[2], std::string("Robert:1"));
    CHECK(*world.manager.game_state(s.code) == GameState::Intermission);
}

TEST_CASE("T04 round closes on the timer with the score unchanged, next question after 5 seconds") {
    World world;
    create(world, 1, "Robert", 3);
    start(world, 1);
    CHECK(world.tick_at(19.9).effects.empty());
    const Outcome result = world.tick_at(0.2);
    CHECK_EQ(result.effects.size(), static_cast<size_t>(1));
    CHECK(is_push(result.effects[0].lines, "ROUND_RESULT 1"));
    CHECK_EQ(result.effects[0].lines[2], std::string("Robert:0"));
    CHECK(world.tick_at(4.9).effects.empty());
    const Outcome next = world.tick_at(0.2);
    CHECK_EQ(next.effects.size(), static_cast<size_t>(1));
    CHECK(is_push(next.effects[0].lines, "QUESTION 2 "));
}

TEST_CASE("T05 scoring ignores speed") {
    for (double delay : {1.0, 18.0}) {
        World world;
        const Session s = create(world, 1, "Robert", 3);
        const Lines question = start(world, 1);
        world.now += delay;
        world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
        CHECK_EQ(*world.manager.player_score(s.code, "Robert"), 1);
    }
}

TEST_CASE("T05 a wrong answer scores nothing and a late tick does not add points") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + wrong_for(question));
    CHECK_EQ(*world.manager.player_score(s.code, "Robert"), 0);
}

TEST_CASE("T06 leaderboard lists highest score first") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    const Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + wrong_for(question));
    world.run(2, std::string("SUBMIT_ANSWER ") + correct_for(question));
    const Outcome board = world.run(1, "LEADERBOARD");
    CHECK(board.effects[0].lines ==
          Lines({"200 OK", "Matthew:1", "Robert:0"}));
}

TEST_CASE("T07 game ends with COMPLETE after the 10 second final intermission") {
    World world;
    const Session s = create(world, 1, "Robert", 2);
    Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    question = tick_to_question(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    CHECK(world.tick_at(9.9).effects.empty());
    const Outcome end = world.tick_at(0.2);
    CHECK_EQ(end.effects.size(), static_cast<size_t>(1));
    CHECK(end.effects[0].lines ==
          Lines({"PUSH GAME_END COMPLETE", "FINAL RESULTS", "Robert:2"}));
    CHECK(end.effects[0].close_after);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(0));
    CHECK(!world.manager.is_joined(1));
    CHECK_EQ(world.run(1, "LEADERBOARD").code, 401);
    CHECK(!world.manager.game_state(s.code).has_value());
}

TEST_CASE("T08 END_GAME replies then pushes GAME_END HOST_ENDED and closes") {
    World world;
    create(world, 1, "Robert", 3);
    start(world, 1);
    const Outcome outcome = world.run(1, "END_GAME");
    CHECK_EQ(outcome.effects.size(), static_cast<size_t>(2));
    CHECK(outcome.effects[0].lines == reply_ok());
    CHECK(!outcome.effects[0].close_after);
    CHECK_EQ(outcome.effects[1].lines[0], std::string("PUSH GAME_END HOST_ENDED"));
    CHECK(outcome.effects[1].close_after);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(0));
    CHECK(world.tick_at(30).effects.empty());  // no further QUESTION pushes
}

TEST_CASE("T09 host QUIT: host gets only 200 and close, others get HOST_QUIT") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    const Outcome outcome = world.run(1, "QUIT");
    CHECK_EQ(count_for(outcome, 1), static_cast<size_t>(1));
    CHECK(effect_for(outcome, 1)->lines == reply_ok());
    CHECK(effect_for(outcome, 1)->close_after);
    CHECK_EQ(count_for(outcome, 2), static_cast<size_t>(1));
    CHECK_EQ(effect_for(outcome, 2)->lines[0], std::string("PUSH GAME_END HOST_QUIT"));
    CHECK(effect_for(outcome, 2)->close_after);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(0));
    CHECK_EQ(world.run(3, "RECONNECT Robert " + s.code + " " + s.token).code, 410);
}

TEST_CASE("T10 non-host QUIT removes only that player") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    const Outcome outcome = world.run(2, "QUIT");
    CHECK(effect_for(outcome, 2)->lines == reply_ok());
    CHECK(effect_for(outcome, 2)->close_after);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(1));
    CHECK(!world.manager.is_joined(2));
    CHECK(world.run(1, "LEADERBOARD").effects[0].lines == Lines({"200 OK", "Robert:0"}));
    CHECK_EQ(world.run(3, "RECONNECT Matthew " + s.code + " " + s.token).code, 410);
}

TEST_CASE("T11 join in the lobby is active immediately with the player count") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Outcome outcome = world.run(2, "JOIN_GAME " + s.code + " Matthew");
    CHECK(effect_for(outcome, 2)->lines[1] == "2 players in game " + s.code);
    CHECK_EQ(effect_for(outcome, 2)->lines[2].size(), static_cast<size_t>(4));
    CHECK(*world.manager.player_state(s.code, "Matthew") == PlayerState::Active);
    // join code is case-insensitive at parse time
    std::string lower = s.code;
    for (char& c : lower) c = static_cast<char>(tolower(c));
    CHECK_EQ(world.run(3, "JOIN_GAME " + lower + " Third").code, 200);
}

TEST_CASE("T17 unknown join code is 404") {
    World world;
    CHECK_EQ(world.run(1, "JOIN_GAME ZZZZ Matthew").code, 404);
    CHECK(!world.manager.is_joined(1));
}

TEST_CASE("T18 commands that need a game return 401 when not joined") {
    World world;
    for (const char* line : {"LEADERBOARD", "SUBMIT_ANSWER A", "START_ROUND", "END_GAME"}) {
        const Outcome outcome = world.run(1, line);
        CHECK_EQ(outcome.code, 401);
        CHECK(outcome.effects[0].lines == reply_error(401));
    }
}

TEST_CASE("T19 wrong-state commands: 420, 421, 422") {
    World world;
    create(world, 1, "Robert", 3);
    CHECK_EQ(world.run(1, "SUBMIT_ANSWER A").code, 420);  // lobby
    CHECK_EQ(world.run(1, "START_ROUND").code, 200);
    CHECK_EQ(world.run(1, "START_ROUND").code, 421);
    // two players so the round stays open after the first answer
    World two;
    const Session s = create(two, 1, "Robert", 3);
    join(two, 2, s.code, "Matthew");
    start(two, 1);
    CHECK_EQ(two.run(1, "SUBMIT_ANSWER A").code, 200);
    CHECK_EQ(two.run(1, "SUBMIT_ANSWER B").code, 422);
}

TEST_CASE("T19 SUBMIT_ANSWER during intermission is 420") {
    World world;
    create(world, 1, "Robert", 3);
    const Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    CHECK_EQ(world.run(1, "SUBMIT_ANSWER A").code, 420);
}

TEST_CASE("T22 a non-host gets 402 for host commands and the game continues") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    CHECK_EQ(world.run(2, "START_ROUND").code, 402);
    start(world, 1);
    CHECK_EQ(world.run(2, "END_GAME").code, 402);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(1));
    CHECK(*world.manager.game_state(s.code) == GameState::QuestionActive);
}

TEST_CASE("T23 duplicate name in a game is 409 but names are case-sensitive") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    CHECK_EQ(world.run(3, "JOIN_GAME " + s.code + " Matthew").code, 409);
    CHECK_EQ(world.run(3, "JOIN_GAME " + s.code + " Robert").code, 409);
    CHECK_EQ(world.run(3, "JOIN_GAME " + s.code + " matthew").code, 200);
}

TEST_CASE("T38 games are isolated: ending game A leaves game B running") {
    World world;
    const Session a = create(world, 1, "Alice", 3);
    const Session b = create(world, 2, "Bob", 3);
    start(world, 2);
    world.run(1, "END_GAME");
    CHECK(!world.manager.game_state(a.code).has_value());
    CHECK(*world.manager.game_state(b.code) == GameState::QuestionActive);
    CHECK(world.manager.is_joined(2));
    CHECK_EQ(world.run(2, "LEADERBOARD").code, 200);
}

TEST_CASE("Slice3 joining mid-round makes a player WAITING until the round result") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    start(world, 1);
    const Outcome joined = world.run(2, "JOIN_GAME " + s.code + " Matthew");
    CHECK_EQ(count_for(joined, 2), static_cast<size_t>(1));  // reply only, no question
    CHECK(*world.manager.player_state(s.code, "Matthew") == PlayerState::Waiting);
    CHECK_EQ(world.run(2, "SUBMIT_ANSWER A").code, 420);
    // The waiting player is not needed to close the round early.
    const Outcome answered = world.run(1, "SUBMIT_ANSWER A");
    CHECK(is_push(answered.effects[1].lines, "ROUND_RESULT"));
    CHECK_EQ(count_for(answered, 2), static_cast<size_t>(0));  // result only to players who played
    CHECK(*world.manager.player_state(s.code, "Matthew") == PlayerState::Active);
    const Lines next = tick_to_question(world, 2);
    CHECK(!next.empty());
}

TEST_CASE("Slice3 joining during intermission is WAITING until the next question, which they receive") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    world.run(2, "JOIN_GAME " + s.code + " Matthew");
    CHECK(*world.manager.player_state(s.code, "Matthew") == PlayerState::Waiting);
    CHECK_EQ(world.run(2, "SUBMIT_ANSWER A").code, 420);  // still no round for them
    const Lines next = tick_to_question(world, 2, 6);     // question 2 itself reaches the newcomer
    CHECK(!next.empty() && is_push(next, "QUESTION 2 "));
    CHECK(*world.manager.player_state(s.code, "Matthew") == PlayerState::Active);
    CHECK_EQ(world.run(2, std::string("SUBMIT_ANSWER ") + correct_for(next)).code, 200);
}

TEST_CASE("G3-2 a correct answer still scores when the player disconnects before the round closes") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    const Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    world.manager.on_disconnect(1, world.now);
    world.tick_at(21);  // Matthew never answers, so the timer closes the round
    CHECK_EQ(*world.manager.player_score(s.code, "Robert"), 1);
    // the restored score is the correct one after a reconnect
    const Outcome back = world.run(3, "RECONNECT Robert " + s.code + " " + s.token);
    CHECK(effect_for(back, 3)->lines == reply_reconnect(1, 1));
}

TEST_CASE("Slice3 round waits for the timer while some active player has not answered") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    start(world, 1);
    world.run(1, "SUBMIT_ANSWER A");
    CHECK(world.tick_at(1).effects.empty());
    CHECK(*world.manager.game_state(s.code) == GameState::QuestionActive);
    world.run(2, "SUBMIT_ANSWER A");
    CHECK(*world.manager.game_state(s.code) == GameState::Intermission);
}

TEST_CASE("T26 unexpected disconnect marks the player pending and keeps the game") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    start(world, 1);
    const Outcome outcome = world.manager.on_disconnect(1, world.now);
    CHECK(outcome.effects.empty());
    CHECK(!world.manager.is_joined(1));
    CHECK(*world.manager.player_state(s.code, "Robert") == PlayerState::DisconnectedPending);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(1));
    CHECK(world.manager.on_disconnect(77, world.now).effects.empty());  // unknown connection
}

TEST_CASE("T27 reconnect within the window restores score and repeats the live question") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    question = tick_to_question(world, 1);  // question 2 is live, score is 1
    world.manager.on_disconnect(1, world.now);
    world.tick_at(10);  // still inside the 20 second round and the 60 second window
    const Outcome outcome = world.run(5, "RECONNECT Robert " + s.code + " " + s.token);
    CHECK_EQ(outcome.code, 200);
    CHECK(effect_for(outcome, 5, 0)->lines == reply_reconnect(1, 2));
    CHECK_EQ(count_for(outcome, 5), static_cast<size_t>(2));
    CHECK(is_push(effect_for(outcome, 5, 1)->lines, "QUESTION 2 "));
    CHECK(world.manager.is_joined(5));
    CHECK_EQ(*world.manager.player_score(s.code, "Robert"), 1);
}

TEST_CASE("Slice3 reconnect in the lobby reports question 0 and pushes nothing") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    world.manager.on_disconnect(1, world.now);
    const Outcome outcome = world.run(2, "RECONNECT Robert " + s.code + " " + s.token);
    CHECK(effect_for(outcome, 2)->lines == reply_reconnect(0, 0));
    CHECK_EQ(outcome.effects.size(), static_cast<size_t>(1));
}

TEST_CASE("Slice3 reconnect during intermission reports the last opened question, no push") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + correct_for(question));
    world.manager.on_disconnect(1, world.now);
    const Outcome outcome = world.run(2, "RECONNECT Robert " + s.code + " " + s.token);
    CHECK(effect_for(outcome, 2)->lines == reply_reconnect(1, 1));
    CHECK_EQ(outcome.effects.size(), static_cast<size_t>(1));
}

TEST_CASE("Slice3 reconnected player cannot answer the same round twice") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 3, s.code, "Matthew");
    start(world, 1);
    world.run(1, "SUBMIT_ANSWER A");
    world.manager.on_disconnect(1, world.now);
    world.run(2, "RECONNECT Robert " + s.code + " " + s.token);
    CHECK_EQ(world.run(2, "SUBMIT_ANSWER B").code, 422);
}

TEST_CASE("T28 reconnect failures are all the identical 410") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    world.manager.on_disconnect(1, world.now);
    const Lines expected = reply_error(410);

    Outcome wrong_token = world.run(2, "RECONNECT Robert " + s.code + " ZZZZ");
    Outcome wrong_name = world.run(2, "RECONNECT Roberta " + s.code + " " + s.token);
    Outcome wrong_code = world.run(2, "RECONNECT Robert ZZZZ " + s.token);
    CHECK(wrong_token.effects[0].lines == expected);
    CHECK(wrong_name.effects[0].lines == expected);
    CHECK(wrong_code.effects[0].lines == expected);

    std::string swapped_case = s.token;
    for (char& c : swapped_case) c = isupper(c) ? static_cast<char>(tolower(c)) : static_cast<char>(toupper(c));
    if (swapped_case != s.token) {
        CHECK(world.run(2, "RECONNECT Robert " + s.code + " " + swapped_case).effects[0].lines == expected);
    }

    world.tick_at(60.5);  // window over
    CHECK(world.run(2, "RECONNECT Robert " + s.code + " " + s.token).effects[0].lines == expected);
    CHECK(!world.manager.is_joined(2));
}

TEST_CASE("T28 expired session is rejected even before the next tick cleans it up") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    world.manager.on_disconnect(1, world.now);
    world.now += 61;
    CHECK_EQ(world.run(2, "RECONNECT Robert " + s.code + " " + s.token).code, 410);
}

TEST_CASE("Slice3 reconnect for a still-connected player is 410 and changes nothing") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Outcome outcome = world.run(2, "RECONNECT Robert " + s.code + " " + s.token);
    CHECK_EQ(outcome.code, 410);
    CHECK_EQ(outcome.effects.size(), static_cast<size_t>(1));
    CHECK(world.manager.is_joined(1));
    CHECK(!world.manager.is_joined(2));
}

TEST_CASE("Slice3 a session can be reconnected only once") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    world.manager.on_disconnect(1, world.now);
    CHECK_EQ(world.run(2, "RECONNECT Robert " + s.code + " " + s.token).code, 200);
    CHECK_EQ(world.run(3, "RECONNECT Robert " + s.code + " " + s.token).code, 410);
}

TEST_CASE("T30 host disconnect does not end the game and host powers survive reconnect") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    start(world, 1);
    world.manager.on_disconnect(1, world.now);
    world.tick_at(25);  // round closed by timer meanwhile, game keeps running
    CHECK(world.manager.game_count() == 1);
    CHECK(*world.manager.game_state(s.code) != GameState::Ended);
    const Outcome back = world.run(2, "RECONNECT Robert " + s.code + " " + s.token);
    CHECK_EQ(back.code, 200);
    const Outcome end = world.run(2, "END_GAME");
    CHECK_EQ(end.code, 200);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(0));
}

TEST_CASE("Slice3 a pending player does not count toward everyone has answered") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    start(world, 1);
    world.manager.on_disconnect(2, world.now);
    world.run(1, "SUBMIT_ANSWER A");
    CHECK(*world.manager.game_state(s.code) == GameState::Intermission);
}

TEST_CASE("Slice3 zero active players waits for the timer instead of closing at once") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    start(world, 1);
    world.manager.on_disconnect(1, world.now);
    world.tick_at(1);
    CHECK(*world.manager.game_state(s.code) == GameState::QuestionActive);
    world.tick_at(19.5);
    CHECK(*world.manager.game_state(s.code) == GameState::Intermission);
}

TEST_CASE("Slice3 game is removed when every player is pending and all windows expire") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    join(world, 2, s.code, "Matthew");
    world.manager.on_disconnect(1, world.now);
    world.tick_at(10);
    world.manager.on_disconnect(2, world.now);
    world.tick_at(55);  // Robert expired (65s), Matthew still inside window
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(1));
    CHECK(!world.manager.player_state(s.code, "Robert").has_value());
    world.tick_at(10);  // Matthew expired too
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(0));
    CHECK_EQ(world.run(3, "RECONNECT Matthew " + s.code + " " + s.token).code, 410);
    CHECK_EQ(world.run(3, "JOIN_GAME " + s.code + " Matthew").code, 404);
}

TEST_CASE("Slice3 already joined connection gets 300 for CREATE, JOIN, RECONNECT without change") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Session other = create(world, 2, "Alice", 3);
    world.manager.on_disconnect(2, world.now);
    CHECK_EQ(world.run(1, "CREATE_GAME Again 2").code, 300);
    CHECK_EQ(world.run(1, "JOIN_GAME " + s.code + " Again").code, 300);
    CHECK_EQ(world.run(1, "RECONNECT Alice " + other.code + " " + other.token).code, 300);
    CHECK_EQ(world.manager.game_count(), static_cast<size_t>(2));
    CHECK(*world.manager.player_state(other.code, "Alice") == PlayerState::DisconnectedPending);
    CHECK(!world.manager.player_state(s.code, "Again").has_value());
}

TEST_CASE("Slice3 QUIT outside a game is 200 with a close effect") {
    World world;
    const Outcome outcome = world.run(1, "QUIT");
    CHECK_EQ(outcome.code, 200);
    CHECK(outcome.effects[0].lines == reply_ok());
    CHECK(outcome.effects[0].close_after);
}

TEST_CASE("Slice3 host QUIT alone sends only the reply with a close effect") {
    World world;
    create(world, 1, "Robert", 3);
    const Outcome outcome = world.run(1, "QUIT");
    CHECK_EQ(outcome.effects.size(), static_cast<size_t>(1));
    CHECK(outcome.effects[0].close_after);
}

TEST_CASE("Slice3 ROUND_RESULT and GAME_END lines are sorted and carry no token") {
    World world;
    const Session s = create(world, 1, "Robert", 2);
    const Session m = join(world, 2, s.code, "Matthew");
    Lines question = start(world, 1);
    world.run(1, std::string("SUBMIT_ANSWER ") + wrong_for(question));
    const Outcome result = world.run(2, std::string("SUBMIT_ANSWER ") + correct_for(question));
    const Effect* push = effect_for(result, 1, 0);
    CHECK(push != nullptr);
    CHECK(push->lines == Lines({"PUSH ROUND_RESULT 1", "Correct answer: " + std::string(1, correct_for(question)), "Matthew:1", "Robert:0"}));
    for (const auto& effect : result.effects) {
        for (const auto& line : effect.lines) {
            CHECK(line.find(s.token) == std::string::npos || s.token.empty());
            CHECK(line.find(m.token) == std::string::npos || m.token.empty());
        }
    }
}

TEST_CASE("T13 log events from game logic never contain a token") {
    World world;
    const Session s = create(world, 1, "Robert", 3);
    const Session m = join(world, 2, s.code, "Matthew");
    start(world, 1);
    world.manager.on_disconnect(1, world.now);
    world.run(3, "RECONNECT Robert " + s.code + " " + s.token);
    world.run(3, "RECONNECT Robert " + s.code + " BAD1");
    world.tick_at(40);
    world.run(3, "END_GAME");
    CHECK(!world.all_events.empty());
    for (const auto& event : world.all_events) {
        CHECK(event.find(s.token) == std::string::npos);
        CHECK(event.find(m.token) == std::string::npos);
    }
}

TEST_CASE("Slice3 timer closes rounds for every game independently") {
    World world;
    const Session a = create(world, 1, "Alice", 3);
    start(world, 1);
    world.now += 10;
    const Session b = create(world, 2, "Bob", 3);
    start(world, 2);
    world.tick_at(10.5);  // game A at 20.5s, game B at 10.5s
    CHECK(*world.manager.game_state(a.code) == GameState::Intermission);
    CHECK(*world.manager.game_state(b.code) == GameState::QuestionActive);
}

#include "server/random_seed.h"

TEST_CASE("Slice3 OS entropy gives different seeds on each call") {
    uint64_t a = 0, b = 0;
    CHECK(os_entropy(a));
    CHECK(os_entropy(b));
    CHECK(a != b);
}
