// Protocol layer: turns one line into a typed Command and builds the exact
// reply and push text. No sockets and no game state.
#ifndef SERVER_PROTOCOL_H
#define SERVER_PROTOCOL_H

#include <array>
#include <string>
#include <utility>
#include <vector>

enum class CommandType {
    CreateGame,
    JoinGame,
    StartRound,
    SubmitAnswer,
    Leaderboard,
    Reconnect,
    Quit,
    EndGame
};

struct Command {
    CommandType type = CommandType::Quit;
    std::string name;   // host or player name
    std::string code;   // join code, normalized to upper case
    std::string token;  // reconnect token, case-sensitive
    int num_questions = 0;
    char choice = 0;    // 'A' to 'D', normalized to upper case
};

struct ParseResult {
    bool ok = false;
    Command command;
    int error_code = 0;  // 300 or 423 when !ok
};

// `available_questions` is how many questions the bank holds (for 423).
ParseResult parse_command(const std::string& line, int available_questions);

const char* command_name(CommandType type);
bool is_valid_name(const std::string& name);

// A message is a list of lines; the Connection adds the blank terminator.
using Lines = std::vector<std::string>;

struct Standing {
    std::string name;
    int score = 0;
};

enum class GameEndReason { Complete, HostEnded, HostQuit };

// First line of a reply for `code`. Only 423 uses `available_questions`.
std::string status_line(int code, int available_questions = 0);

Lines reply_ok();
Lines reply_error(int code, int available_questions = 0);
Lines reply_create(const std::string& game_code, const std::string& token);
Lines reply_join(int player_count, const std::string& game_code, const std::string& token);
Lines reply_leaderboard(const std::vector<Standing>& standings);
Lines reply_reconnect(int score, int question_number);

Lines push_question(int number, const std::string& text, const std::array<std::string, 4>& options);
Lines push_round_result(int number, char correct, const std::vector<Standing>& standings);
Lines push_game_end(GameEndReason reason, const std::vector<Standing>& standings);

// Highest score first; equal scores keep their original (join) order.
std::vector<Standing> sort_standings(std::vector<Standing> standings);

#endif
