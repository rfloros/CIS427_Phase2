# Shared helpers for the shell tests. Source this file; do not run it.
# Each script gets its own scratch directory and cleans up its server on exit.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SERVER="$ROOT/server"
CLIENT="$ROOT/client"
RAW="$ROOT/build/raw_client"
WORK="$(mktemp -d)"
SERVER_PID=""
FAILURES=0

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill -KILL "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    jobs -p | xargs -r kill -KILL 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILURES=$((FAILURES + 1)); }

# check NAME CONDITION...: runs the condition and records the result.
check() {
    local name="$1"
    shift
    if "$@"; then pass "$name"; else fail "$name"; fi
}

contains() { case "$1" in *"$2"*) return 0 ;; esac; return 1; }
lacks() { ! contains "$1" "$2"; }

# True if nothing is listening on the port.
port_is_free() { ! (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; }

# start_server [server options...]: picks a free port (retrying if another
# process grabs it first) and waits until the server logs that it is listening.
start_server() {
    local attempt
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        PORT=$((20000 + RANDOM % 20000))
        port_is_free "$PORT" || continue
        LOG="$WORK/server.log"
        : > "$LOG"
        "$SERVER" "$PORT" --log "$LOG" --questions "$ROOT/questions.txt" "$@" > "$WORK/server.out" 2>&1 &
        SERVER_PID=$!
        local waited=0
        while [ $waited -lt 50 ]; do
            if grep -q "listening on" "$LOG" 2>/dev/null; then return 0; fi
            kill -0 "$SERVER_PID" 2>/dev/null || break
            sleep 0.1
            waited=$((waited + 1))
        done
        kill -KILL "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
        SERVER_PID=""
    done
    echo "could not start the server"
    return 1
}

# raw STEP...: talk to the running server with the raw-socket helper.
raw() { "$RAW" 127.0.0.1 "$PORT" "$@"; }

wait_for_log() {
    local text="$1" tries=0
    while [ $tries -lt 100 ]; do
        grep -q "$text" "$LOG" && return 0
        sleep 0.05
        tries=$((tries + 1))
    done
    return 1
}

# Waits until more than COUNT log lines contain TEXT.
wait_for_more_log_lines() {
    local text="$1" count="$2" tries=0
    while [ $tries -lt 100 ]; do
        [ "$(grep -c "$text" "$LOG")" -gt "$count" ] && return 0
        sleep 0.05
        tries=$((tries + 1))
    done
    return 1
}

# Waits for a process to exit; prints its exit status. Gives up after N tenths of a second.
wait_exit() {
    local pid="$1" tenths="${2:-50}" tries=0
    while kill -0 "$pid" 2>/dev/null; do
        [ $tries -ge "$tenths" ] && return 1
        sleep 0.1
        tries=$((tries + 1))
    done
    wait "$pid"
    return $?
}

finish() {
    if [ $FAILURES -eq 0 ]; then
        echo "all checks passed"
        exit 0
    fi
    echo "$FAILURES check(s) failed"
    exit 1
}
