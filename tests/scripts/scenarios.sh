#!/usr/bin/env bash
# Slice 7: drives the real server and client binaries through the failure
# scenarios (T14, T15, T21, T24, T25, T26, T27, T29, T31, T32, T33, T13, SIGINT).
# Rerun it any time, for example during the demo: bash tests/scripts/scenarios.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

LONG_TIMERS=(--question-secs 5 --intermission-secs 1 --final-intermission-secs 1 --reconnect-secs 10)

start_server "${LONG_TIMERS[@]}" || exit 1

# ---- Framing and bad input over a raw socket -------------------------------
out=$(raw send $'\n' read 300)
check "T14 empty line gets 300" test "$out" = $'300 message format error'

out=$(raw send 'HELLO\n' send 'LEADERBOARD\n' read 300)
check "T15 unknown command gets 300 and the connection stays usable" \
    test "$out" = $'300 message format error\n\n401 You are not currently in a game, join or reconnect first.'

out=$(raw send 'CREATE_GAME Robert 9999\n' send 'CREATE_GAME\n' read 300)
check "T16 bad count gets 423 and missing name gets 300" \
    test "$out" = $'423 Requested question count must be between 1 and 26.\n\n300 message format error'

out=$(raw sendrep x 600 send '\nLEADERBOARD\n' read 400)
check "T21 over-length line gets one 300, then the next command works" \
    test "$out" = $'300 message format error\n\n401 You are not currently in a game, join or reconnect first.'

out=$(raw send 'CREATE_GAME Rob' sleep 500 send 'ert 3\n' read 300)
reply_shape='^200 OK
[A-Z0-9]{4}
[A-Za-z0-9]{4}$'
check "T31 command split across two sends gets a 200 with code and token" \
    bash -c '[[ "$1" =~ $2 ]]' _ "$out" "$reply_shape"
out_one=$(printf '%s' "$out" | grep -c '^200 OK$')
check "T31 exactly one reply" test "$out_one" = 1
check "T31 no 300 error" lacks "$out" "300"

out=$(raw send 'CREATE_GAME Robert 3\n' sleep 200 send 'LEADERBOARD\nLEADERBOARD\n' read 400)
check "T32 two commands in one send give two ordered replies" \
    contains "$out" $'200 OK\nRobert:0\n\n200 OK\nRobert:0'


out=$(raw bytewise 'CREATE_GAME Robert 3\n' 10 read 300)
check "T33 one byte at a time gives the create reply" contains "$out" "200 OK"

# ---- Demo requests: unknown command and bad count, with matching log lines ---
wait_for_log "client .* protocol error 300" ; found_300=$?
wait_for_log "client .* protocol error 423" ; found_423=$?
check "T13 log has the 300 protocol error" test $found_300 -eq 0
check "T13 log has the 423 protocol error" test $found_423 -eq 0

# ---- T24 / T25: client start-up failures -----------------------------------
dead_port=$((41000 + RANDOM % 1000))
port_is_free $dead_port || dead_port=$((dead_port + 1))
err=$("$CLIENT" 127.0.0.1 $dead_port < /dev/null 2>&1 >/dev/null); status=$?
check "T24 no server: clear message" contains "$err" "Could not connect to 127.0.0.1:$dead_port"
check "T24 no server: non-zero exit" test $status -ne 0

connects_before=$(grep -c "connected from" "$LOG")
for bad_port in abc 0 70000; do
    err=$("$CLIENT" 127.0.0.1 $bad_port < /dev/null 2>&1 >/dev/null); status=$?
    check "T25 port $bad_port is refused with a message" contains "$err" "invalid port"
    check "T25 port $bad_port exits non-zero" test $status -ne 0
done
connects_after=$(grep -c "connected from" "$LOG")
check "T25 no connection attempt for a bad port" test "$connects_before" = "$connects_after"

# ---- T26 / T27: kill the real client, reconnect with the saved session -----
mkdir "$WORK/player" && cd "$WORK/player" || exit 1
mkfifo keyboard
"$CLIENT" 127.0.0.1 "$PORT" < keyboard > first.out 2>&1 &
client_pid=$!
exec 3> keyboard
printf 'create_game Robert 3\nstart_round\n' >&3
sleep 0.7
check "T26 session file is private" test "$(stat -c %a .termtrivia_session)" = 600
disconnects_before=$(grep -c "disconnected" "$LOG")
kill -KILL $client_pid
exec 3>&-
wait $client_pid 2>/dev/null
wait_for_more_log_lines "disconnected" "$disconnects_before"
check "T26 server logs the disconnect" test $? -eq 0
check "T26 server keeps listening" contains "$(raw send 'LEADERBOARD\n' read 300)" "401"

{ printf 'reconnect\n'; sleep 0.8; printf 'leaderboard\n'; sleep 0.4; printf 'quit\n'; sleep 0.4; } \
    | "$CLIENT" 127.0.0.1 "$PORT" > second.out 2>&1
check "T27 reconnect works with the saved session" contains "$(cat second.out)" "Reconnected"
check "T27 the live question is pushed again" contains "$(cat second.out)" "QUESTION 1"
check "T27 leaderboard still shows the player" contains "$(cat second.out)" "Robert:0"
check "T27 QUIT removes the session file" test ! -e .termtrivia_session
token=$(grep -o 'Join code: [A-Z0-9]*' first.out | head -1)
check "T01 client printed a join code" test -n "$token"
cd "$ROOT"

# ---- T29: stop the server while the client sits at its prompt ---------------
mkdir "$WORK/idle" && cd "$WORK/idle" || exit 1
mkfifo keyboard
"$CLIENT" 127.0.0.1 "$PORT" < keyboard > idle.out 2>&1 &
client_pid=$!
exec 3> keyboard
sleep 0.5
kill -KILL "$SERVER_PID"; wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""
wait_exit $client_pid 50; status=$?
exec 3>&-
check "T29 client exits cleanly when the server goes away" test $status -eq 0
check "T29 client says the server closed the connection" contains "$(cat idle.out)" "Server closed the connection"
cd "$ROOT"

# ---- SIGINT: shuts down with no stray protocol message ----------------------
start_server "${LONG_TIMERS[@]}" || exit 1
kill -INT "$SERVER_PID"
wait_exit "$SERVER_PID" 30; status=$?
SERVER_PID=""
check "SIGINT while blocked in accept exits 0" test $status -eq 0
check "SIGINT logs shutdown" grep -q "server shutting down" "$LOG"

start_server "${LONG_TIMERS[@]}" || exit 1
(raw send 'CREATE_GAME Robert 3\n' read 3000 > "$WORK/sigint_client.out") &
raw_pid=$!
sleep 0.5
kill -INT "$SERVER_PID"
wait_exit "$SERVER_PID" 30; status=$?
SERVER_PID=""
wait $raw_pid 2>/dev/null
check "SIGINT while blocked in recv exits 0" test $status -eq 0
check "SIGINT sends no protocol push" lacks "$(cat "$WORK/sigint_client.out")" "PUSH"

# ---- Tokens never reach the log ---------------------------------------------
start_server "${LONG_TIMERS[@]}" || exit 1
out=$(raw send 'CREATE_GAME Robert 3\n' read 300)
secret=$(printf '%s\n' "$out" | sed -n '3p')
code=$(printf '%s\n' "$out" | sed -n '2p')
raw send "RECONNECT Robert $code ZZZZ\n" read 200 > /dev/null
raw send "RECONNECT Robert $code $secret\n" read 200 > /dev/null
check "NFR-09 the token is a real value" test ${#secret} -eq 4
check "NFR-09 no token in the log" lacks "$(cat "$LOG")" "$secret"

finish
