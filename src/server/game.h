// Game logic: games, players, scoring, and timed rounds. It never touches a
// socket. Every operation takes the current time and returns "effects": the
// messages the server layer must deliver, in order, to which connection.
#ifndef SERVER_GAME_H
#define SERVER_GAME_H

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "common/protocol.h"
#include "server/question_bank.h"

using ConnId = uint64_t;

// All durations are in seconds; tests and demos may use decimals.
struct Timings {
    double question = 20;
    double intermission = 5;
    double final_intermission = 10;
    double reconnect = 60;
};

enum class GameState { Lobby, QuestionActive, Intermission, Ended };
enum class PlayerState { Active, Waiting, DisconnectedPending };

// One message for one connection. `close_after` tells the server layer to
// close that connection once the message has been written.
struct Effect {
    ConnId conn = 0;
    Lines lines;
    bool close_after = false;
};

struct Outcome {
    int code = 200;                        // reply code, for the request log
    std::vector<Effect> effects;           // deliver in this order
    std::vector<std::string> events;       // log lines; never contain tokens
};

class GameManager {
public:
    GameManager(std::vector<Question> questions, Timings timings, uint64_t seed);

    Outcome handle(ConnId conn, const Command& command, double now);
    // The connection dropped without a QUIT.
    Outcome on_disconnect(ConnId conn, double now);
    // Advances timers: closes rounds, opens questions, ends games, expires
    // reconnect windows. Call it often (every ~50 ms).
    Outcome tick(double now);

    int available_questions() const;

    // Read-only views, used by tests and the request log.
    size_t game_count() const;
    bool is_joined(ConnId conn) const;
    std::optional<GameState> game_state(const std::string& code) const;
    std::optional<PlayerState> player_state(const std::string& code, const std::string& name) const;
    std::optional<int> player_score(const std::string& code, const std::string& name) const;

private:
    struct Player {
        std::string name;
        std::string token;
        int score = 0;
        PlayerState state = PlayerState::Active;
        ConnId conn = 0;  // 0 while disconnected
        bool is_host = false;
        bool answered = false;
        char answer = 0;
        double disconnected_at = 0;
    };

    struct Game {
        std::string code;
        GameState state = GameState::Lobby;
        std::vector<Player> players;      // in join order
        std::vector<size_t> question_order;
        int current_number = 0;           // number of the last opened question
        double phase_ends_at = 0;
    };

    // All of these assume mutex_ is held.
    Outcome create_game(ConnId conn, const Command& command);
    Outcome join_game(ConnId conn, const Command& command);
    Outcome start_round(ConnId conn, double now);
    Outcome submit_answer(ConnId conn, const Command& command, double now);
    Outcome leaderboard(ConnId conn);
    Outcome reconnect(ConnId conn, const Command& command, double now);
    Outcome quit(ConnId conn);
    Outcome end_game_command(ConnId conn);

    Outcome reply_only(ConnId conn, int code, bool close_after = false);
    Outcome reply_only(ConnId conn, int code, const Lines& lines, bool close_after = false);

    Game* find_game(const std::string& code);
    const Game* find_game(const std::string& code) const;
    Game* game_of(ConnId conn, Player** player);
    static Player* find_player(Game& game, const std::string& name);

    void open_question(Game& game, double now, Outcome& outcome);
    void close_round(Game& game, double now, Outcome& outcome);
    bool round_is_complete(const Game& game) const;
    void end_game(Game& game, GameEndReason reason, ConnId skip_conn, Outcome& outcome);
    void expire_pending_players(Game& game, double now, Outcome& outcome);
    void advance_game(Game& game, double now, Outcome& outcome);
    std::vector<Standing> standings(const Game& game) const;

    std::string random_text(const std::string& alphabet, size_t length);
    std::string unique_code();

    mutable std::mutex mutex_;
    std::vector<Question> questions_;
    Timings timings_;
    std::mt19937_64 random_;
    std::map<std::string, Game> games_;
    std::map<ConnId, std::string> game_code_of_conn_;
};

#endif
