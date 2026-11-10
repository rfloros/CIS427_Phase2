#!/usr/bin/env bash
# T51 restart behavior: stops the server with a game in progress, starts it
# again on the same port, and checks what the players see. Games live only in
# memory, so after a restart an old game and its tokens must be gone, the old
# connections must be closed, and the new server must accept new games at once.
# Both a clean stop (SIGINT) and a crash (SIGKILL) are checked.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

# read_reply FD: prints one reply (lines up to the blank line that ends it).
read_reply() {
    local fd=$1 line out=""
    while IFS= read -r -t 3 line <&"$fd"; do
        [ -z "$line" ] && break
        out+="$line"$'\n'
    done
    printf '%s' "${out%$'\n'}"
}

# True if the connection was closed by the server (end of file).
# Pushes already sent are read first; the connection counts as closed only if
# the reads end with end-of-file and not with a timeout.
connection_closed() {
    local fd=$1 line status
    while true; do
        IFS= read -r -t 3 line <&"$fd"
        status=$?
        [ $status -eq 0 ] || break
    done
    [ $status -eq 1 ]
}

# Starts the server again on the port that start_server chose.
restart_server() {
    "$SERVER" "$PORT" --log "$LOG" --questions "$ROOT/questions.txt" \
        --question-secs 5 --intermission-secs 1 --final-intermission-secs 1 --reconnect-secs 10 \
        > "$WORK/server.out" 2>&1 &
    SERVER_PID=$!
    local tries=0
    while [ $tries -lt 50 ]; do
        [ "$(grep -c 'listening on' "$LOG")" -ge "$RESTARTS" ] && return 0
        kill -0 "$SERVER_PID" 2>/dev/null || return 1
        sleep 0.1
        tries=$((tries + 1))
    done
    return 1
}

# One round: start a game with a host and a player, stop the server the given
# way, restart it, and check the old players and a new game.
one_restart() {
    local how="$1" label="$2"
    RESTARTS=$((RESTARTS + 1))

    exec {host}<>"/dev/tcp/127.0.0.1/$PORT"
    printf 'CREATE_GAME Robert 3\n' >&"$host"
    reply=$(read_reply "$host")
    code=$(sed -n 2p <<< "$reply")
    host_token=$(sed -n 3p <<< "$reply")

    exec {guest}<>"/dev/tcp/127.0.0.1/$PORT"
    printf 'JOIN_GAME %s Bob\n' "$code" >&"$guest"
    reply=$(read_reply "$guest")
    guest_token=$(sed -n 3p <<< "$reply")

    printf 'START_ROUND\n' >&"$host"
    read_reply "$host" > /dev/null
    check "T51 $label: the game was running before the stop" \
        wait_for_more_log_lines "question 1 opened" $((RESTARTS - 2))

    if [ "$how" = "clean" ]; then kill -INT "$SERVER_PID"; else kill -KILL "$SERVER_PID"; fi
    wait "$SERVER_PID" 2>/dev/null
    SERVER_PID=""
    check "T51 $label: the host connection is closed" connection_closed "$host"
    check "T51 $label: the player connection is closed" connection_closed "$guest"
    exec {host}>&- {guest}>&-

    check "T51 $label: the server starts again on the same port" restart_server

    # The old session is gone: reconnecting with the old name, code, and token fails.
    exec {old}<>"/dev/tcp/127.0.0.1/$PORT"
    printf 'RECONNECT Robert %s %s\n' "$code" "$host_token" >&"$old"
    reply=$(read_reply "$old")
    check "T51 $label: the old host token gets 410" test "${reply%% *}" = "410"
    printf 'RECONNECT Bob %s %s\n' "$code" "$guest_token" >&"$old"
    reply=$(read_reply "$old")
    check "T51 $label: the old player token gets 410" test "${reply%% *}" = "410"
    printf 'JOIN_GAME %s Carol\n' "$code" >&"$old"
    reply=$(read_reply "$old")
    check "T51 $label: the old game code gets 404" test "${reply%% *}" = "404"

    # A new game works on the new server, on the same connection.
    printf 'CREATE_GAME Robert 2\n' >&"$old"
    reply=$(read_reply "$old")
    check "T51 $label: a new game can be created" test "$(sed -n 1p <<< "$reply")" = "200 OK"
    exec {old}>&-
}

RESTARTS=1
start_server --question-secs 5 --intermission-secs 1 --final-intermission-secs 1 --reconnect-secs 10 || exit 1

one_restart clean "clean stop"
check "T51 the clean stop logged the shutdown" grep -q "server shutting down" "$LOG"

one_restart crash "crash"

check "T51 the restarted server logged its start each time" \
    test "$(grep -c 'server starting' "$LOG")" -eq 3
finish
