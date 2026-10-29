#!/usr/bin/env bash
# Creates the single draft zip for the Phase 3 submission: documents, source,
# tests, build files, README, question bank, and the multi-client sample log.
# It leaves out build output, binaries, local logs, session files, and the
# workflow folders. The zip stays a draft until the team's Ripple reflection
# and contribution summary are written into the report.
# Usage: bash scripts/package-phase3.sh [output.zip] [--verify-build]
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="$root/dist/termtrivia-phase3-draft.zip"
verify=0
for arg in "$@"; do
    case "$arg" in
        --verify-build) verify=1 ;;
        *) output="$arg" ;;
    esac
done

mkdir -p "$(dirname "$output")"
rm -f "$output"
cd "$root" || exit 1
zip -qr -X "$output" README.md Makefile questions.txt src tests scripts docs \
    -x '*.o' '*.d' '*/server.log' '*/.termtrivia_session' 'server.log' '.termtrivia_session' \
    || exit 1
echo "created $output"

[ "$verify" = 1 ] || exit 0
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
unzip -q "$output" -d "$scratch/termtrivia" || exit 1
(cd "$scratch/termtrivia" && make all && make test) || { echo "build or tests failed in the extracted zip"; exit 1; }
echo "extracted zip builds and passes its tests"
