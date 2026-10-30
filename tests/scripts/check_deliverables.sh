#!/usr/bin/env bash
# T48 to T50: the Phase 3 deliverables exist and match the handout: report
# sections, README fields, diagrams, sample log, demo guide, and the draft zip.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$ROOT" || exit 1

has() { grep -qiF -- "$2" "$1"; }

# ---- T48 technical report ---------------------------------------------------
report=docs/phase3-technical-report.html
for section in "Revised Curiosity Report" "Final problem statement and value proposition" \
    "Final requirements" "Architecture diagram" "Sequence diagram" "Concurrency design" \
    "Protocol specification" "Data structures" "Error-handling strategy" "Test results" \
    "Known limitations" "Scalability analysis" "Team contribution summary" "Ripple Report reflection"; do
    check "T48 report section: $section" grep -q "<h2[^>]*>[0-9. ]*$section" "$report"
done
check "T48 contribution summary is marked pending" test "$(grep -c 'Pending student input' "$report")" -ge 2
check "T48 report PDF exists and has pages" bash -c 'pdfinfo docs/phase3-technical-report.pdf | grep -q "^Pages: *[1-9]"'
check "T48 original Curiosity Report is kept" test -s docs/curiosity-report-original.txt
check "T48 revised Curiosity keeps the classmate interviews" has docs/curiosity-report-revised.html "2 classmates"
check "T48 report embeds the revised Curiosity text" has "$report" "2 classmates"
for image in architecture_diagram sequence_diagram; do
    for ext in svg png pdf; do
        check "T48 diagram docs/images/$image.$ext" test -s "docs/images/$image.$ext"
    done
    check "T48 report shows $image" has "$report" "$image.png"
done

# ---- README (handout list) --------------------------------------------------
for field in "Project name and purpose" "Team members" "Required software" "Build instructions" \
    "Server startup" "Client startup" "Command-line arguments" "Example interaction" \
    "Test instructions" "Known limitations" "Attribution"; do
    check "README field: $field" grep -qi "^## .*$field" README.md
done

# ---- T49 demo guide, script, and sample log ---------------------------------
for item in "Server startup" "Three simultaneous client connections" "Client identities" \
    "Shared application state" "Concurrent user operations" "Conflicting or overlapping operation" \
    "Correct synchronization" "Unexpected client disconnect" "Continued operation" \
    "Malformed or invalid request" "Graceful connection termination" "Server logs" \
    "Distinctive value-creating feature" "Scalability discussion" "Proposed speaking parts"; do
    check "T49 demo item: $item" grep -qi "^#.*$item" docs/phase3-demo.md
done
check "T49 demo proposes parts for Robert" grep -q "Robert" docs/phase3-demo.md
check "T49 demo proposes parts for Matthew" grep -q "Matthew" docs/phase3-demo.md
check "T49 demo marks speaking parts as proposals" has docs/phase3-demo.md "to confirm"
check "T49 sample log shows several clients" test "$(grep -c 'connected from' docs/sample_server.log)" -ge 3
check "T49 sample log shows a conflict and a shutdown" bash -c \
    'grep -q "JOIN_GAME -> 409" docs/sample_server.log && grep -q "server shutting down" docs/sample_server.log'
check "T49 sample log has no token-like secrets" bash -c '! grep -Eqi "token" docs/sample_server.log'

# ---- T50 draft zip ----------------------------------------------------------
zip_path="$WORK/draft.zip"
check "T50 package script creates one zip" bash scripts/package-phase3.sh "$zip_path"
rel_dir="$WORK/rel-out"
rel_path="termtrivia-rel-check/nested/rel.zip"
mkdir -p "$rel_dir"
# Never run the check if the repo already has that path: a regression could overwrite it.
check "T50 relative-path check will not touch an existing repo path" \
    bash -c '! test -e "$1/termtrivia-rel-check"' _ "$ROOT"
if [ ! -e "$ROOT/termtrivia-rel-check" ]; then
    check "T50 relative output path is relative to the caller's directory" bash -c \
        'cd "$1" && bash "$2/scripts/package-phase3.sh" "$3" >/dev/null && test -s "$1/$3" && ! test -e "$2/termtrivia-rel-check"' \
        _ "$rel_dir" "$ROOT" "$rel_path"
fi
listing=$(unzip -Z1 "$zip_path" 2>/dev/null)
for needed in README.md Makefile questions.txt src/server/server.cpp tests/minitest.h \
    docs/phase3-technical-report.pdf docs/curiosity-report-original.txt \
    docs/curiosity-report-revised.html docs/images/architecture_diagram.pdf \
    docs/images/sequence_diagram.pdf docs/sample_server.log docs/phase3-demo.md docs/protocol.md; do
    check "T50 zip contains $needed" bash -c 'grep -qxF "$1" <<<"$2"' _ "$needed" "$listing"
done
for banned in '^\.git/' '^\.agentloop/' '^build/' '^dist/' '^server\.log$' '^\.termtrivia_session$' \
    '^server$' '^client$' '\.o$'; do
    check "T50 zip excludes $banned" bash -c '! grep -Eq "$1" <<<"$2"' _ "$banned" "$listing"
done
finish
