#!/usr/bin/env bash
# Runs the live-demo script from docs/implementation-plan.md without typing:
# real server with default timers, real client for the game, the raw-socket
# client for the invalid requests and framing cases, then the matching log lines.
# Usage: bash scripts/demo.sh
source "$(dirname "${BASH_SOURCE[0]}")/../tests/scripts/lib.sh"

step() { printf '\n===== %s =====\n' "$1"; }

step "1. Start the server with default timers and show the first log lines"
start_server || exit 1
head -3 "$LOG"

step "2. Start the client and connect: the command list appears"
mkdir "$WORK/player" && cd "$WORK/player" || exit 1
mkfifo keyboard
"$CLIENT" 127.0.0.1 "$PORT" < keyboard > client.out 2>&1 &
client_pid=$!
exec 3> keyboard
sleep 0.5
sed -n 1,11p client.out

step "3. CREATE_GAME with 2 questions, then START_ROUND: the question arrives on its own"
printf 'create_game Robert 2\n' >&3
sleep 0.4
printf 'start_round\n' >&3
sleep 0.6
grep -A5 "^QUESTION 1" client.out

step "4. Answer, see the round result push, then LEADERBOARD"
printf 'submit_answer a\n' >&3
sleep 0.5
printf 'leaderboard\n' >&3
sleep 0.4
grep -A3 "^ROUND_RESULT 1" client.out
tail -4 client.out | grep -v '^Commands:'

step "5a. The client rejects mistakes itself and keeps working"
printf 'dance\ncreate_game Robert\nleaderboard\n' >&3
sleep 0.5
tail -8 client.out | grep -E "Unknown command|Usage:|Robert:"

step "6a. Kill the client (as if it crashed)"
kill -KILL $client_pid
exec 3>&-
wait $client_pid 2>/dev/null
echo "client killed; saved session file mode: $(stat -c %a .termtrivia_session)"

step "5b. Raw-socket client sends two invalid requests to the server"
raw send 'HELLO\n' send 'CREATE_GAME Robert 9999\n' send 'LEADERBOARD\n' read 400

step "7. Framing: a command split across two sends, then two commands in one send"
echo "-- split send --"
raw send 'CREATE_GAME Rob' sleep 500 send 'ert 3\n' read 300
echo "-- two commands in one send --"
raw send 'CREATE_GAME Robert 3\n' sleep 200 send 'LEADERBOARD\nLEADERBOARD\n' read 400

step "6b. Restart the client and RECONNECT with the saved session: the score is kept"
{ printf 'reconnect\n'; sleep 0.8; printf 'leaderboard\n'; sleep 0.4; printf 'quit\n'; sleep 0.5; } \
    | "$CLIENT" 127.0.0.1 "$PORT" 2>&1 | grep -E "Reconnected|QUESTION|Robert:|200 OK"

step "8. The matching server log lines"
kill -INT "$SERVER_PID"
wait_exit "$SERVER_PID" 30
SERVER_PID=""
cat "$LOG"
