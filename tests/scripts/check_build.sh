#!/usr/bin/env bash
# Slice 0: both programs must exist after the build that make test depends on.
set -u
cd "$(dirname "$0")/../.."
fail=0
for program in server client; do
    if [ ! -x "./$program" ]; then
        echo "FAIL missing ./$program"
        fail=1
    fi
done
[ $fail -eq 0 ] && echo "PASS build produced server and client"
exit $fail
