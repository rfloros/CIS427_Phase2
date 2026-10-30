#!/usr/bin/env bash
# Phase 3 demo: one server, five simultaneous raw-socket clients, one game.
# Shows startup, identities, shared state, simultaneous answers, a duplicate-name
# race, an unexpected disconnect with reconnect, an invalid request, graceful
# termination, and the server log. About 20 seconds.
# Usage: bash scripts/demo_phase3.sh [--save-log FILE]
source "$(dirname "${BASH_SOURCE[0]}")/../tests/scripts/lib.sh"

save_log=""
[ "${1:-}" = "--save-log" ] && save_log="$2"

step() { printf '\n===== %s =====\n' "$1"; }

# Hides each private token (the line after the join code or the player count).
hide_tokens() {
    awk '{ if (mask) { print "<private token>"; mask = 0; next }
           print
           if ($0 ~ /players in game/ || (prev == "200 OK" && $0 ~ /^[A-Z0-9]{4}$/)) mask = 1
           prev = $0 }' "$1"
}
show() { printf -- '--- %s ---\n' "$1"; hide_tokens "$WORK/$2.out"; }

step "1. Start the server (default question bank replaced by a demo bank where A is always right)"
start_server --seed 7 --questions "$ROOT/scripts/demo_questions.txt" \
    --question-secs 4 --intermission-secs 1 --final-intermission-secs 2 --reconnect-secs 30 || exit 1
echo "server pid $SERVER_PID, port $PORT"
head -2 "$LOG"

step "2. Robert connects (client 1) and creates a game with 3 questions"
raw send 'CREATE_GAME Robert 3\n' sleep 3000 send 'START_ROUND\n' sleep 300 send 'SUBMIT_ANSWER A\n' \
    sleep 3900 send 'SUBMIT_ANSWER A\n' sleep 600 send 'LEADERBOARD\n' sleep 100 send 'HELLO\n' \
    closewait 20000 > "$WORK/robert.out" &
robert_pid=$!
wait_for_log "game .* created" || exit 1
code=$(sed -n 2p "$WORK/robert.out")
echo "join code: $code"

step "3. One second later Matthew, Alice, Bob join at once; two more clients race for the name Sam"
sleep 1
raw send "JOIN_GAME $code Matthew\n" sleep 2300 send 'SUBMIT_ANSWER A\n' sleep 3900 send 'SUBMIT_ANSWER A\n' \
    closewait 20000 > "$WORK/matthew.out" &
matthew_pid=$!
raw send "JOIN_GAME $code Alice\n" sleep 4500 send 'SUBMIT_ANSWER A\n' sleep 1900 send 'SUBMIT_ANSWER A\n' \
    closewait 20000 > "$WORK/alice.out" &
alice_pid=$!
raw send "JOIN_GAME $code Bob\n" sleep 2300 send 'SUBMIT_ANSWER B\n' sleep 3700 kill > "$WORK/bob.out" &
bob_pid=$!
raw send "JOIN_GAME $code Sam\n" read 800 send 'QUIT\n' read 300 > "$WORK/sam1.out" &
raw send "JOIN_GAME $code Sam\n" read 800 send 'QUIT\n' read 300 > "$WORK/sam2.out" &
echo "Five clients are now connected at once. The script plays the game on a timer:"
echo "  START_ROUND, then question 1: Robert, Matthew answer A and Bob answers B together; Alice answers A two"
echo "  seconds later. Bob's connection is killed during question 2 and he reconnects between questions."

step "4. Wait while the game plays out (about 14 seconds)"
wait "$bob_pid"
sleep 1.4
token=$(sed -n 3p "$WORK/bob.out")
raw send "RECONNECT Bob $code $token\n" send 'LEADERBOARD\n' read 1800 > "$WORK/bob2.out" &
wait "$robert_pid" "$matthew_pid" "$alice_pid"

step "5. What the clients saw"
show "Sam racer 1 (conflicting JOIN_GAME: exactly one racer gets 200, the other 409)" sam1
show "Sam racer 2" sam2
show "Bob, first connection (killed during question 2)" bob
show "Bob, new connection after RECONNECT: score kept, game state shared, no restart" bob2
show "Alice (identity and answers; her late answer scored the same as Matthew's quick one)" alice
show "Robert (host): all pushes, the invalid request HELLO -> 300, then GAME_END and close" robert

step "6. Ctrl+C: graceful shutdown, then the server log"
kill -INT "$SERVER_PID"
wait_exit "$SERVER_PID" 30
SERVER_PID=""
cat "$LOG"
if grep -q "$token" "$LOG"; then echo "ERROR: a token appears in the log"; exit 1; fi
[ -n "$save_log" ] && cp "$LOG" "$save_log" && echo "saved log to $save_log"
exit 0
