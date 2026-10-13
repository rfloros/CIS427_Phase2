#!/usr/bin/env bash
# Slice 0: questions.txt must hold at least 20 valid questions.
# A valid line has six non-empty ASCII fields without extra bars and a
# correct letter of A to D.
set -u
cd "$(dirname "$0")/../.."
file="${1:-questions.txt}"
valid=$(LC_ALL=C awk -F'|' '
    /^[[:space:]]*$/ || /^#/ { next }
    NF == 6 && $1 != "" && $2 != "" && $3 != "" && $4 != "" && $5 != "" &&
    $6 ~ /^[ABCD]$/ && $0 !~ /[^ -~]/ { count++ }
    END { print count + 0 }' "$file")
if [ "$valid" -ge 20 ]; then
    echo "PASS $file holds $valid valid questions"
else
    echo "FAIL $file holds only $valid valid questions"
    exit 1
fi
