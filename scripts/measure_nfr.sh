#!/usr/bin/env bash
# Measures the NFR-01 limits with 20 healthy localhost clients and the real
# 20/5/10/60 second timers: command reply time, question push time, and the
# round timer's accuracy. Takes about 35 seconds.
# Usage: bash scripts/measure_nfr.sh
source "$(dirname "${BASH_SOURCE[0]}")/../tests/scripts/lib.sh"

now_ms() { date +%s%3N; }
players=19

start_server || exit 1

host_start=$(now_ms)
raw --stamp send 'CREATE_GAME Host 2\n' sleep 3500 send 'LEADERBOARD\n' sleep 300 send 'START_ROUND\n' \
    read 27000 kill > "$WORK/host.out" &
wait_for_log "game .* created" || exit 1
code=$(grep -v '>>' "$WORK/host.out" | sed -n 2p | sed -E 's/^\[[^]]*\] //')

sleep 1
for i in $(seq 1 $players); do
    echo "$(now_ms)" > "$WORK/p$i.start"
    raw --stamp send "JOIN_GAME $code P$i\n" read 27500 kill > "$WORK/p$i.out" &
done
wait $(jobs -p | grep -v "^$SERVER_PID$") 2>/dev/null

# Stamped lines look like "1234 text"; ">> sent ..." marks each send.
# Prefixes look like "[+1234ms]"; this prints the number.
stamp_of() { grep -F "$2" "$1" | sed -n "${3:-1}p" | sed -E 's/^\[\+([0-9]+)ms\].*/\1/'; }
board_sent=$(stamp_of "$WORK/host.out" ">> sent LEADERBOARD")
board_reply=$(sed -E 's/^\[\+([0-9]+)ms\] /\1 /' "$WORK/host.out" | awk -v s="$board_sent" '$1 >= s && /Host:0/ {print $1; exit}')
start_sent=$(stamp_of "$WORK/host.out" ">> sent START_ROUND")
start_reply=$(sed -E 's/^\[\+([0-9]+)ms\] /\1 /' "$WORK/host.out" | awk -v s="$start_sent" '$1 >= s && /200 OK/ {print $1; exit}')
q1=$(stamp_of "$WORK/host.out" "PUSH QUESTION 1")
r1=$(stamp_of "$WORK/host.out" "PUSH ROUND_RESULT 1")
host_abs_q1=$((host_start + q1))

worst_push=0
for i in $(seq 1 $players); do
    base=$(cat "$WORK/p$i.start")
    seen=$(stamp_of "$WORK/p$i.out" "PUSH QUESTION 1")
    [ -z "$seen" ] && { echo "player $i never saw question 1"; continue; }
    delay=$((base + seen - host_start - start_sent))
    [ $delay -gt $worst_push ] && worst_push=$delay
done

echo "players connected: $((players + 1))"
echo "LEADERBOARD reply time (20 players in game): $((board_reply - board_sent)) ms (limit 500)"
echo "START_ROUND reply time: $((start_reply - start_sent)) ms (limit 500)"
echo "START_ROUND sent to question 1 seen by the slowest of 20 clients: ${worst_push} ms (limit 1000)"
echo "question 1 to its round result (timer 20 s): $((r1 - q1)) ms (limit 20000 +/- 1000)"
