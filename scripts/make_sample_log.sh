#!/usr/bin/env bash
# Runs a short real session against the real server (default timers) and saves
# the server's own log, unedited, as docs/sample_server.log (or the path given).
# The session covers startup, a connection, several requests, two errors, a
# client that vanishes, a reconnect, a host QUIT, and a Ctrl+C shutdown.
# Usage: bash scripts/make_sample_log.sh [output-file]
source "$(dirname "${BASH_SOURCE[0]}")/../tests/scripts/lib.sh"
output="${1:-$ROOT/docs/sample_server.log}"

start_server || exit 1

created=$(raw send 'CREATE_GAME Robert 3\n' sleep 300 \
    send 'START_ROUND\n' sleep 300 \
    send 'HELLO\n' sleep 200 \
    send 'CREATE_GAME Robert 9999\n' sleep 200 \
    send 'SUBMIT_ANSWER A\n' sleep 300 \
    send 'LEADERBOARD\n' sleep 300 kill)
code=$(printf '%s\n' "$created" | sed -n '2p')
token=$(printf '%s\n' "$created" | sed -n '3p')

sleep 1.5
raw send "RECONNECT Robert $code $token\n" sleep 300 send 'LEADERBOARD\n' sleep 300 send 'QUIT\n' read 800 > /dev/null

kill -INT "$SERVER_PID"
wait_exit "$SERVER_PID" 30
SERVER_PID=""

if grep -q "$token" "$LOG"; then
    echo "refusing to save: a token appears in the log"
    exit 1
fi
cp "$LOG" "$output"
echo "saved $output"
