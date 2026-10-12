# Project rules: TermTrivia

These rules reflect the repository before implementation begins. Re-run
Discovery when source code, tests, and build files have been added.

## Risk paths

- `docs/protocol.md`: public TCP protocol contract, including command formats,
  response codes, message framing, and reconnect behavior.

No database, payment, outbound messaging, deployment, infrastructure, or secret
files exist in the repository at this point.

## Diff patterns to re-triage

- Added or changed lines matching
  `DEBUG|SECRET|TOKEN|PASSWORD|TIMEOUT|RETRY|RATE_LIMIT|feature.?flag`
  (case-insensitive).
- Deletions under `tests/`, once that directory exists.

## Size threshold

Recommend Gate 1 for a change likely to exceed 300 changed lines or 8 files.

## Project checks

- Lint/build command: `make all`. The planned Makefile compiles C++17 with
  `-Wall -Wextra`; the build must have no warnings.
- Test command: `make test`.

The Makefile and test runner are planned for Slice 0 and do not exist yet.
