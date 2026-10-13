#include "server/game.h"

#include <algorithm>
#include <numeric>

namespace {

const std::string kCodeAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const std::string kTokenAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
const size_t kCodeLength = 4;
const size_t kTokenLength = 4;

}  // namespace

GameManager::GameManager(std::vector<Question> questions, Timings timings, uint64_t seed)
    : questions_(std::move(questions)), timings_(timings), random_(seed) {}

int GameManager::available_questions() const { return static_cast<int>(questions_.size()); }

// ---------------------------------------------------------------- dispatch

Outcome GameManager::handle(ConnId conn, const Command& command, double now) {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (command.type) {
        case CommandType::CreateGame: return create_game(conn, command);
        case CommandType::JoinGame: return join_game(conn, command);
        case CommandType::StartRound: return start_round(conn, now);
        case CommandType::SubmitAnswer: return submit_answer(conn, command, now);
        case CommandType::Leaderboard: return leaderboard(conn);
        case CommandType::Reconnect: return reconnect(conn, command, now);
        case CommandType::Quit: return quit(conn);
        case CommandType::EndGame: return end_game_command(conn);
    }
    return reply_only(conn, 300);
}

Outcome GameManager::on_disconnect(ConnId conn, double now) {
    std::lock_guard<std::mutex> lock(mutex_);
    Outcome outcome;
    Player* player = nullptr;
    Game* game = game_of(conn, &player);
    if (game == nullptr) return outcome;  // not in a game, or the game already ended

    player->state = PlayerState::DisconnectedPending;
    player->conn = 0;
    player->disconnected_at = now;
    game_code_of_conn_.erase(conn);
    outcome.events.push_back("game " + game->code + ": player " + player->name +
                             " disconnected, reconnect window open");
    return outcome;
}

Outcome GameManager::tick(double now) {
    std::lock_guard<std::mutex> lock(mutex_);
    Outcome outcome;
    std::vector<std::string> codes;
    for (const auto& entry : games_) codes.push_back(entry.first);

    for (const auto& code : codes) {
        Game* game = find_game(code);
        expire_pending_players(*game, now, outcome);
        if (game->players.empty()) {
            outcome.events.push_back("game " + code + " removed: no players left");
            games_.erase(code);
            continue;
        }
        advance_game(*game, now, outcome);
    }
    return outcome;
}

// ---------------------------------------------------------------- commands

Outcome GameManager::create_game(ConnId conn, const Command& command) {
    if (game_code_of_conn_.count(conn)) return reply_only(conn, 300);
    if (command.num_questions < 1 || command.num_questions > available_questions()) {
        return reply_only(conn, 423);
    }

    Game game;
    game.code = unique_code();

    Player host;
    host.name = command.name;
    host.token = random_text(kTokenAlphabet, kTokenLength);
    host.conn = conn;
    host.is_host = true;
    game.players.push_back(host);

    std::vector<size_t> order(questions_.size());
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), random_);
    order.resize(static_cast<size_t>(command.num_questions));
    game.question_order = order;

    Outcome outcome = reply_only(conn, 200, reply_create(game.code, host.token));
    outcome.events.push_back("game " + game.code + " created by " + host.name + " with " +
                             std::to_string(command.num_questions) + " questions");
    game_code_of_conn_[conn] = game.code;
    games_[game.code] = game;
    return outcome;
}

Outcome GameManager::join_game(ConnId conn, const Command& command) {
    if (game_code_of_conn_.count(conn)) return reply_only(conn, 300);
    Game* game = find_game(command.code);
    if (game == nullptr) return reply_only(conn, 404);
    if (find_player(*game, command.name) != nullptr) return reply_only(conn, 409);

    Player player;
    player.name = command.name;
    player.token = random_text(kTokenAlphabet, kTokenLength);
    player.conn = conn;
    // Players who arrive mid-game wait for the next question.
    player.state = game->state == GameState::Lobby ? PlayerState::Active : PlayerState::Waiting;
    game->players.push_back(player);
    game_code_of_conn_[conn] = game->code;

    Outcome outcome = reply_only(
        conn, 200, reply_join(static_cast<int>(game->players.size()), game->code, player.token));
    outcome.events.push_back("game " + game->code + ": " + player.name + " joined");
    return outcome;
}

Outcome GameManager::start_round(ConnId conn, double now) {
    Player* player = nullptr;
    Game* game = game_of(conn, &player);
    if (game == nullptr) return reply_only(conn, 401);
    if (!player->is_host) return reply_only(conn, 402);
    if (game->state != GameState::Lobby) return reply_only(conn, 421);

    // The reply is queued first so it is always written before the question.
    Outcome outcome = reply_only(conn, 200);
    open_question(*game, now, outcome);
    return outcome;
}

Outcome GameManager::submit_answer(ConnId conn, const Command& command, double now) {
    Player* player = nullptr;
    Game* game = game_of(conn, &player);
    if (game == nullptr) return reply_only(conn, 401);
    if (game->state != GameState::QuestionActive || player->state != PlayerState::Active) {
        return reply_only(conn, 420);
    }
    if (player->answered) return reply_only(conn, 422);

    player->answered = true;
    player->answer = command.choice;
    Outcome outcome = reply_only(conn, 200);
    if (round_is_complete(*game)) close_round(*game, now, outcome);
    return outcome;
}

Outcome GameManager::leaderboard(ConnId conn) {
    Player* player = nullptr;
    Game* game = game_of(conn, &player);
    if (game == nullptr) return reply_only(conn, 401);
    return reply_only(conn, 200, reply_leaderboard(standings(*game)));
}

Outcome GameManager::reconnect(ConnId conn, const Command& command, double now) {
    if (game_code_of_conn_.count(conn)) return reply_only(conn, 300);

    // Every failure gives the same 410 so a guesser learns nothing.
    Game* game = find_game(command.code);
    if (game == nullptr) return reply_only(conn, 410);
    Player* player = find_player(*game, command.name);
    if (player == nullptr || player->state != PlayerState::DisconnectedPending ||
        now - player->disconnected_at > timings_.reconnect || player->token != command.token) {
        return reply_only(conn, 410);
    }

    player->conn = conn;
    player->state = PlayerState::Active;
    game_code_of_conn_[conn] = game->code;

    Outcome outcome = reply_only(conn, 200, reply_reconnect(player->score, game->current_number));
    if (game->state == GameState::QuestionActive) {
        const Question& question = questions_[game->question_order[game->current_number - 1]];
        outcome.effects.push_back(
            {conn, push_question(game->current_number, question.text, question.options), false});
    }
    outcome.events.push_back("game " + game->code + ": player " + player->name + " reconnected");
    return outcome;
}

Outcome GameManager::quit(ConnId conn) {
    Player* player = nullptr;
    Game* game = game_of(conn, &player);
    if (game == nullptr) return reply_only(conn, 200, true);  // QUIT outside a game

    Outcome outcome = reply_only(conn, 200, true);
    if (player->is_host) {
        end_game(*game, GameEndReason::HostQuit, conn, outcome);
        return outcome;
    }

    outcome.events.push_back("game " + game->code + ": " + player->name + " quit");
    const std::string name = player->name;
    game->players.erase(std::remove_if(game->players.begin(), game->players.end(),
                                       [&](const Player& p) { return p.name == name; }),
                        game->players.end());
    game_code_of_conn_.erase(conn);
    return outcome;
}

Outcome GameManager::end_game_command(ConnId conn) {
    Player* player = nullptr;
    Game* game = game_of(conn, &player);
    if (game == nullptr) return reply_only(conn, 401);
    if (!player->is_host) return reply_only(conn, 402);

    Outcome outcome = reply_only(conn, 200);
    end_game(*game, GameEndReason::HostEnded, 0, outcome);
    return outcome;
}

// ---------------------------------------------------------------- helpers

Outcome GameManager::reply_only(ConnId conn, int code, bool close_after) {
    const Lines lines = code == 200 ? reply_ok() : reply_error(code, available_questions());
    return reply_only(conn, code, lines, close_after);
}

Outcome GameManager::reply_only(ConnId conn, int code, const Lines& lines, bool close_after) {
    Outcome outcome;
    outcome.code = code;
    outcome.effects.push_back({conn, lines, close_after});
    return outcome;
}

GameManager::Game* GameManager::find_game(const std::string& code) {
    const auto it = games_.find(code);
    return it == games_.end() ? nullptr : &it->second;
}

const GameManager::Game* GameManager::find_game(const std::string& code) const {
    const auto it = games_.find(code);
    return it == games_.end() ? nullptr : &it->second;
}

GameManager::Game* GameManager::game_of(ConnId conn, Player** player) {
    const auto mapped = game_code_of_conn_.find(conn);
    if (mapped == game_code_of_conn_.end()) return nullptr;
    Game* game = find_game(mapped->second);
    if (game == nullptr) return nullptr;
    for (auto& candidate : game->players) {
        if (candidate.conn == conn) {
            *player = &candidate;
            return game;
        }
    }
    return nullptr;
}

GameManager::Player* GameManager::find_player(Game& game, const std::string& name) {
    for (auto& player : game.players) {
        if (player.name == name) return &player;
    }
    return nullptr;
}

std::vector<Standing> GameManager::standings(const Game& game) const {
    std::vector<Standing> list;
    for (const auto& player : game.players) list.push_back({player.name, player.score});
    return sort_standings(list);
}

bool GameManager::round_is_complete(const Game& game) const {
    int active = 0;
    for (const auto& player : game.players) {
        if (player.state != PlayerState::Active) continue;
        if (!player.answered) return false;
        ++active;
    }
    // With nobody active we wait for the timer instead (design-changes.md, #15).
    return active > 0;
}

void GameManager::open_question(Game& game, double now, Outcome& outcome) {
    ++game.current_number;
    game.state = GameState::QuestionActive;
    game.phase_ends_at = now + timings_.question;

    const Question& question = questions_[game.question_order[game.current_number - 1]];
    const Lines push = push_question(game.current_number, question.text, question.options);
    for (auto& player : game.players) {
        player.answered = false;
        player.answer = 0;
        // A player who joined during the intermission starts with this question.
        if (player.state == PlayerState::Waiting) player.state = PlayerState::Active;
        if (player.state == PlayerState::Active) outcome.effects.push_back({player.conn, push, false});
    }
    outcome.events.push_back("game " + game.code + ": question " +
                             std::to_string(game.current_number) + " opened");
}

void GameManager::close_round(Game& game, double now, Outcome& outcome) {
    const Question& question = questions_[game.question_order[game.current_number - 1]];
    // An accepted answer counts even if the player has disconnected since.
    for (auto& player : game.players) {
        if (player.answered && player.answer == question.correct) ++player.score;
    }

    const Lines result = push_round_result(game.current_number, question.correct, standings(game));
    for (auto& player : game.players) {
        if (player.state == PlayerState::Active) outcome.effects.push_back({player.conn, result, false});
    }
    // Players who arrived mid-round take part from the next question on.
    for (auto& player : game.players) {
        if (player.state == PlayerState::Waiting) player.state = PlayerState::Active;
    }

    const bool was_last = game.current_number >= static_cast<int>(game.question_order.size());
    game.state = GameState::Intermission;
    game.phase_ends_at = now + (was_last ? timings_.final_intermission : timings_.intermission);
    outcome.events.push_back("game " + game.code + ": round " +
                             std::to_string(game.current_number) + " closed");
}

void GameManager::end_game(Game& game, GameEndReason reason, ConnId skip_conn, Outcome& outcome) {
    const Lines message = push_game_end(reason, standings(game));
    for (const auto& player : game.players) {
        if (player.conn == 0) continue;
        if (player.conn != skip_conn) outcome.effects.push_back({player.conn, message, true});
        game_code_of_conn_.erase(player.conn);
    }
    const char* reason_text = reason == GameEndReason::Complete    ? "COMPLETE"
                              : reason == GameEndReason::HostEnded ? "HOST_ENDED"
                                                                   : "HOST_QUIT";
    outcome.events.push_back("game " + game.code + " ended: " + reason_text);
    games_.erase(game.code);  // also drops its pending reconnect sessions
}

void GameManager::expire_pending_players(Game& game, double now, Outcome& outcome) {
    const auto expired = [&](const Player& player) {
        return player.state == PlayerState::DisconnectedPending &&
               now - player.disconnected_at > timings_.reconnect;
    };
    for (const auto& player : game.players) {
        if (expired(player)) {
            outcome.events.push_back("game " + game.code + ": reconnect window expired for " +
                                     player.name);
        }
    }
    game.players.erase(std::remove_if(game.players.begin(), game.players.end(), expired),
                       game.players.end());
}

void GameManager::advance_game(Game& game, double now, Outcome& outcome) {
    if (game.state == GameState::QuestionActive) {
        if (now >= game.phase_ends_at || round_is_complete(game)) close_round(game, now, outcome);
    } else if (game.state == GameState::Intermission && now >= game.phase_ends_at) {
        if (game.current_number >= static_cast<int>(game.question_order.size())) {
            end_game(game, GameEndReason::Complete, 0, outcome);
        } else {
            open_question(game, now, outcome);
        }
    }
}

std::string GameManager::random_text(const std::string& alphabet, size_t length) {
    std::uniform_int_distribution<size_t> pick(0, alphabet.size() - 1);
    std::string text;
    for (size_t i = 0; i < length; ++i) text += alphabet[pick(random_)];
    return text;
}

std::string GameManager::unique_code() {
    while (true) {
        const std::string code = random_text(kCodeAlphabet, kCodeLength);
        if (games_.find(code) == games_.end()) return code;
    }
}

// ---------------------------------------------------------------- read-only views

size_t GameManager::game_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return games_.size();
}

bool GameManager::is_joined(ConnId conn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return game_code_of_conn_.count(conn) > 0;
}

std::optional<GameState> GameManager::game_state(const std::string& code) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Game* game = find_game(code);
    if (game == nullptr) return std::nullopt;
    return game->state;
}

std::optional<PlayerState> GameManager::player_state(const std::string& code,
                                                     const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Game* game = find_game(code);
    if (game == nullptr) return std::nullopt;
    for (const auto& player : game->players) {
        if (player.name == name) return player.state;
    }
    return std::nullopt;
}

std::optional<int> GameManager::player_score(const std::string& code,
                                             const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Game* game = find_game(code);
    if (game == nullptr) return std::nullopt;
    for (const auto& player : game->players) {
        if (player.name == name) return player.score;
    }
    return std::nullopt;
}
