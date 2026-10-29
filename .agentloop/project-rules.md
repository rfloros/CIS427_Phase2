# Project rules: TermTrivia

These rules reflect the implemented C++17 client and server, their TCP
protocol, and the current build and test targets.

## Risk paths

- `docs/protocol.md` and `src/common/protocol.h`,
  `src/common/protocol.cpp`: public TCP protocol contract, including command
  formats, responses, pushes, and reconnect behavior.
- `src/server/game.h`, `src/server/game.cpp`, `src/server/server.h`, and
  `src/server/server.cpp`: shared game state, player identity and reconnect
  tokens, session lifetime, synchronization, and game removal.

No database, payment, outbound messaging, dependency manifest, CI/deploy,
infrastructure, or environment/secret files are present in the repository.

## Diff patterns to re-triage

- Added or changed lines matching
  `DEBUG|SECRET|TOKEN|PASSWORD|TIMEOUT|RETRY|RATE_LIMIT|feature.?flag`
  (case-insensitive).
- Deletions under `tests/`.

## Size threshold

Recommend Gate 1 for a change likely to exceed 300 changed lines or 8 files.

## Project checks

- Lint/build command: `make all`. The Makefile builds the client and server
  with C++17, `-Wall`, and `-Wextra`; the build must have no warnings.
- Test command: `make test`. This builds the programs and runs unit,
  integration, and scripted tests. The separate `make test-default-timers`
  target runs the slow real-duration timer check when timing behavior needs it.
