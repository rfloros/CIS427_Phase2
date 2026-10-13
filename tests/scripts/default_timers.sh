#!/usr/bin/env bash
# Slow check (about 2.5 minutes) against the real 20/5/10/60 second defaults.
# Records T02 push latency, reply latency, T04 timer accuracy, T07 final
# intermission, T30 (reconnect after 25 s), and T28c (reconnect after the 60 s
# window). Run it with: make test-default-timers
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

# ms_of PATTERN FILE: the stamp of the first line matching PATTERN.
ms_of() { grep -m1 -- "$1" "$2" | sed -E 's/^\[\+([0-9]+)ms\].*/\1/'; }
within() { # within ACTUAL_MS TARGET_MS TOLERANCE_MS
    local diff=$(( $1 - $2 )); [ ${diff#-} -le "$3" ]
}

start_server || exit 1

# ---- Part A: one two-question game, nobody answers --------------------------
out="$WORK/timed.out"
raw --stamp send 'CREATE_GAME Robert 2\n' sleep 300 send 'START_ROUND\n' closewait 60000 > "$out"

sent_start=$(ms_of '>> sent START_ROUND' "$out")
# the second 200 OK is the START_ROUND reply (the first answers CREATE_GAME)
reply_ms=$(awk '/200 OK/{c++; if(c==2){print; exit}}' "$out" | sed -E 's/^\[\+([0-9]+)ms\].*/\1/')
q1=$(ms_of 'PUSH QUESTION 1' "$out")
r1=$(ms_of 'PUSH ROUND_RESULT 1' "$out")
q2=$(ms_of 'PUSH QUESTION 2' "$out")
r2=$(ms_of 'PUSH ROUND_RESULT 2' "$out")
end=$(ms_of 'PUSH GAME_END COMPLETE' "$out")

echo "T02 START_ROUND reply latency:      $((reply_ms - sent_start)) ms (limit 500)"
echo "T02 first question push latency:    $((q1 - sent_start)) ms (limit 1000)"
echo "T04 question window:                $((r1 - q1)) ms (target 20000 +/- 1000)"
echo "T04 intermission before question 2: $((q2 - r1)) ms (target 5000 +/- 1000)"
echo "T07 final question window:          $((r2 - q2)) ms (target 20000 +/- 1000)"
echo "T07 final intermission:             $((end - r2)) ms (target 10000 +/- 1000)"

check "T02 reply within 500 ms" test $((reply_ms - sent_start)) -le 500
check "T02 first question within 1 s" test $((q1 - sent_start)) -le 1000
check "T04 question timer accurate" within $((r1 - q1)) 20000 1000
check "T04 intermission timer accurate" within $((q2 - r1)) 5000 1000
check "T07 final intermission accurate" within $((end - r2)) 10000 1000
check "T04 score unchanged when nobody answers" grep -q 'Robert:0' "$out"

# ---- Part B: T30, reconnect 25 s after a disconnect -------------------------
created=$(raw send 'CREATE_GAME Robert 1\n' sleep 200 send 'START_ROUND\n' read 300 kill)
code=$(printf '%s\n' "$created" | sed -n '2p')
token=$(printf '%s\n' "$created" | sed -n '3p')
echo "waiting 25 seconds before reconnecting (T30)..."
sleep 25
back=$(raw send "RECONNECT Robert $code $token\n" sleep 300 send 'END_GAME\n' read 500)
check "T30 host reconnects after 25 s" contains "$back" "200 OK"
check "T30 host powers kept: END_GAME accepted" contains "$back" "GAME_END HOST_ENDED"

# ---- Part C: T28c, correct details after the 60 s window --------------------
# 3 questions keep the game running past 61 s, so only the window can cause the 410.
created=$(raw send 'CREATE_GAME Robert 3\n' sleep 200 send 'START_ROUND\n' read 300 kill)
code=$(printf '%s\n' "$created" | sed -n '2p')
token=$(printf '%s\n' "$created" | sed -n '3p')
echo "waiting 61 seconds for the reconnect window to expire (T28c)..."
sleep 61
late=$(raw send "RECONNECT Robert $code $token\n" read 500)
check "T28c reconnect after 61 s is the generic 410" \
    contains "$late" "410 Wrong name, game code, or token, or the reconnect window has expired."

finish
