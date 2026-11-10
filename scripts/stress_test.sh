#!/usr/bin/env bash
# T52 load test: many clients in several games at once, each sending many
# requests, one request at a time, and waiting for every reply. Records the
# number of clients and requests, the duration, how many requests got the
# expected reply, how many failed, and the response times.
#
# Every client sends mostly LEADERBOARD (expects "200 OK") and one in ten
# requests is an invalid line, HELLO (expects "300 message format error"),
# so the run also checks that bad input does not disturb the other clients.
# The games stay in the lobby, so no timed pushes arrive between replies.
#
# Usage: bash scripts/stress_test.sh [games] [players_per_game] [requests_per_client]
# Defaults: 5 games, 10 players per game plus a host each, 200 requests per client
# (55 clients, 11 000 requests). Takes well under a minute on a laptop.
# Use only against your own server on localhost.
source "$(dirname "${BASH_SOURCE[0]}")/../tests/scripts/lib.sh"

games=${1:-5}
players=${2:-10}
requests=${3:-200}
reply_timeout=5   # seconds to wait for one reply before counting a failure

now_us() { local t=$EPOCHREALTIME; echo "${t/./}"; }

# read_reply FD: reads lines up to the blank line that ends every reply and
# prints the first line. Returns 1 on timeout or end of file.
read_reply() {
    local fd=$1 first="" line
    IFS= read -r -t "$reply_timeout" first <&"$fd" || return 1
    while IFS= read -r -t "$reply_timeout" line <&"$fd"; do
        [ -z "$line" ] && break
    done
    printf '%s\n' "$first"
}

# drive FD OUTFILE: sends REQUESTS requests on FD, one at a time. Writes one
# line per request to OUTFILE: "ok MICROSECONDS" or "fail".
drive() {
    local fd=$1 out=$2 i start reply expected
    : > "$out"
    for ((i = 1; i <= requests; i++)); do
        if ((i % 10 == 0)); then
            expected="300 message format error"
            start=$(now_us)
            printf 'HELLO\n' >&"$fd" 2>/dev/null
        else
            expected="200 OK"
            start=$(now_us)
            printf 'LEADERBOARD\n' >&"$fd" 2>/dev/null
        fi
        reply=$(read_reply "$fd")
        if [ "$reply" = "$expected" ]; then
            echo "ok $(($(now_us) - start))" >> "$out"
        else
            echo "fail" >> "$out"
            return
        fi
    done
}

start_server || exit 1
echo "server pid $SERVER_PID, port $PORT"

# One host per game, created in this shell so the connections stay open.
declare -a codes
for ((g = 0; g < games; g++)); do
    exec {hostfd}<>"/dev/tcp/127.0.0.1/$PORT" || exit 1
    printf 'CREATE_GAME Host%d 3\n' "$g" >&"$hostfd"
    reply_lines=$(for _ in 1 2 3 4; do IFS= read -r -t 5 l <&"$hostfd"; echo "$l"; done)
    codes[g]=$(sed -n 2p <<< "$reply_lines")
    hostfds[g]=$hostfd
    [ ${#codes[g]} -eq 4 ] || { echo "could not create game $g"; exit 1; }
done

joined=0
start_all=$(now_us)

# Players: each connects, joins its game, then runs the request loop.
for ((g = 0; g < games; g++)); do
    for ((p = 1; p <= players; p++)); do
        (
            exec 6<>"/dev/tcp/127.0.0.1/$PORT" || { echo "fail" > "$WORK/g${g}p${p}.out"; exit 1; }
            printf 'JOIN_GAME %s G%dP%d\n' "${codes[g]}" "$g" "$p" >&6
            join_reply=$(read_reply 6)
            if [ "$join_reply" != "200 OK" ]; then echo "joinfail" > "$WORK/g${g}p${p}.out"; exit 1; fi
            echo joined > "$WORK/g${g}p${p}.joined"
            drive 6 "$WORK/g${g}p${p}.out"
        ) &
    done
    # The host also sends requests.
    ( drive "${hostfds[g]}" "$WORK/g${g}host.out" ) &
done

# Sample the server's thread count and memory while the clients run.
max_threads=0
max_rss_kb=0
while [ -n "$(jobs -rp | grep -v "^$SERVER_PID$")" ]; do
    t=$(awk '/^Threads:/ {print $2}' "/proc/$SERVER_PID/status" 2>/dev/null)
    r=$(awk '/^VmRSS:/ {print $2}' "/proc/$SERVER_PID/status" 2>/dev/null)
    [ "${t:-0}" -gt "$max_threads" ] && max_threads=$t
    [ "${r:-0}" -gt "$max_rss_kb" ] && max_rss_kb=$r
    sleep 0.2
done
end_all=$(now_us)

joined=$(ls "$WORK"/*.joined 2>/dev/null | wc -l)
clients=$((games * (players + 1)))
duration_ms=$(((end_all - start_all) / 1000))

summary=$(cat "$WORK"/*.out | awk '$1 == "ok" {print $2}' | sort -n | awk '
    { v[NR] = $1; sum += $1 }
    END {
        if (NR == 0) { print "0 0 0 0 0"; exit }
        printf "%d %.2f %.2f %.2f %.2f\n", NR, sum / NR / 1000, v[int(NR * 0.50) + 1] / 1000, v[int(NR * 0.95) + 1] / 1000, v[NR] / 1000
    }')
bad_count=$(cat "$WORK"/*.out | grep -c -E '^(fail|joinfail)')
read -r ok_count avg_ms p50_ms p95_ms max_ms <<< "$summary"
total=$((ok_count + bad_count))

# Graceful stop under load history: SIGINT should exit 0 and log the shutdown.
alive_before=0
kill -0 "$SERVER_PID" 2>/dev/null && alive_before=1
kill -INT "$SERVER_PID"
wait_exit "$SERVER_PID" 50
stop_status=$?
SERVER_PID=""
shutdown_logged=0
grep -q "server shutting down" "$LOG" && shutdown_logged=1
errors_logged=$(grep -c " ERROR " "$LOG")

echo
echo "clients connected (hosts + players):      $clients ($games games, $players players and a host each)"
echo "clients that joined their game:           $((joined + games)) of $clients"
echo "requests sent (one at a time per client): $((clients * requests)) planned, $total answered or failed"
echo "successful requests (expected reply):     $ok_count"
echo "failed requests (wrong reply or timeout): $bad_count"
echo "duration:                                 ${duration_ms} ms"
echo "throughput:                               $((ok_count * 1000 / (duration_ms > 0 ? duration_ms : 1))) requests per second"
echo "response time, average / median / 95th / max: ${avg_ms} / ${p50_ms} / ${p95_ms} / ${max_ms} ms"
echo "server threads at peak:                   $max_threads"
echo "server memory (RSS) at peak:              $((max_rss_kb / 1024)) MiB"
echo "server alive after the run:               $alive_before (1 = yes)"
echo "server exit status after SIGINT:          $stop_status"
echo "shutdown line in the log:                 $shutdown_logged (1 = yes)"
echo "ERROR lines in the log:                   $errors_logged"
echo

check "T52 every client joined its game" test "$((joined + games))" -eq "$clients"
check "T52 every request got its expected reply" test "$bad_count" -eq 0 -a "$ok_count" -eq "$((clients * requests))"
check "T52 the server survived the load" test "$alive_before" -eq 1
check "T52 the server stops cleanly after the load" test "$stop_status" -eq 0 -a "$shutdown_logged" -eq 1
check "T52 no ERROR lines in the log" test "$errors_logged" -eq 0
finish
