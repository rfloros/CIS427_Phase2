#!/usr/bin/env bash
# Runs the Phase 3 demo (five simultaneous clients, a name race, an invalid
# request, a killed client that reconnects, a full game, Ctrl+C) and saves the
# server's own log, unedited, as docs/sample_server.log (or the path given).
# The demo refuses to save a log that contains a token.
# Usage: bash scripts/make_sample_log.sh [output-file]
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-$root/docs/sample_server.log}"
bash "$root/scripts/demo_phase3.sh" --save-log "$output" > /dev/null
