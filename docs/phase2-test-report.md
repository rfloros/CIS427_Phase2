# TermTrivia Phase 2 test report

Date of the final runs: 2026-10-12. Machine: Linux 7.2.5, `g++ (GCC) 16.2.1`, C++17,
`-Wall -Wextra`, no warnings. Every row of the test plan has its actual result in
`docs/test-plan.md`; this report summarizes how they were produced.

## What was run

| Command | What it covers | Result |
|---|---|---|
| `make all` (from a clean tree) | build with `-std=c++17 -Wall -Wextra -pthread` | 0 warnings, `server` and `client` built |
| `make test` | unit tests, real-socket tests, shell scenarios | all pass |
| `build/unit_tests` | 110 tests: framing, protocol, question bank, game logic with a fake clock, logging, config | 110 passed, 0 check failures |
| `build/integration_tests` | 50 tests: real server over sockets with fast timers; the real `Client` class against a fake server; startup failures | 50 passed, 0 check failures |
| `bash tests/scripts/scenarios.sh` | the real binaries: bad input, framing, no server, bad ports, killed client and reconnect, server stopped, Ctrl+C, token never logged | 36 checks passed |
| `build/integration_tests` x 10 | repeated stability runs | 10 of 10 runs: 50 tests, 0 failures |
| `make test-default-timers` | real 20/5/10/60 second timers | 9 checks passed (below) |
| `bash scripts/demo.sh` | the live-demo script end to end | succeeded, output matches the log lines |

## Timing measurements with the default timers

From `make test-default-timers` (localhost, nobody answering):

| Measurement | Target | Measured |
|---|---|---|
| Reply to START_ROUND (T02, NFR-01) | under 500 ms | 1 ms |
| First question after START_ROUND (T02) | under 1000 ms | 1 ms |
| Question 1 open until ROUND_RESULT 1 (T04) | 20 s +/- 1 s | 20.016 s |
| ROUND_RESULT 1 until QUESTION 2 (T04) | 5 s +/- 1 s | 5.020 s |
| Question 2 open until ROUND_RESULT 2 | 20 s +/- 1 s | 20.008 s |
| Final ROUND_RESULT until GAME_END COMPLETE (T07) | 10 s +/- 1 s | 10.003 s |
| Host reconnects 25 s after a disconnect (T30) | game running, host powers kept | 200 OK, END_GAME accepted |
| Reconnect with correct details 61 s after a disconnect (T28c) | identical 410 | 410 |

Score stayed unchanged when nobody answered (T04). T05 (scoring ignores speed) is verified at the
logic level with a fake clock: a correct answer after 1 s and after 18 s both score exactly 1.

## Stress and race checks (in `build/integration_tests`)

- **T35:** 50 fresh games, the `200 OK` reply came before `PUSH QUESTION 1` every time.
- **Reply-before-result:** 40 games where SUBMIT_ANSWER closes the round at once; the `200 OK`
  always preceded `ROUND_RESULT`.
- **Reconnect racing the round close:** up to 40 reconnects with 50 ms rounds; a `QUESTION n` never
  followed `ROUND_RESULT n`.
- **Timer GAME_END racing client commands:** 15 runs of LEADERBOARD spam against a game that
  ends on the timer; the complete `GAME_END` always arrived before EOF.
- **Descriptor reuse:** 30 rapid games on fresh connections (half ended by the timer, half by
  QUIT); the first message on every new connection was its own reply, and the server accepted a
  new client afterwards.
- **Ctrl+C:** while blocked in `accept` and while blocked in `recv`: exit status 0, `server
  shutting down` logged, no protocol message sent.
- **Tokens:** the unit, socket, and shell tests search the log for real tokens; none appear.

## Test plan coverage

| Group | Tests | Status |
|---|---|---|
| Normal operation | T01 to T09, T12, T13 | Pass |
| Normal operation, Phase 3 | T10, T11 | logic level Pass; socket test with two clients pending (Phase 3) |
| Invalid input | T14 to T21 | Pass |
| Invalid input, Phase 3 | T22, T23 | logic level Pass; socket test pending (Phase 3) |
| Networking failures | T24 to T30 | Pass |
| Framing | T31 to T35 | Pass |
| Concurrency (Phase 3) | T36, T37, T39 | not run, pending (Phase 3); T38 logic level Pass, socket pending |

## Notes and known limits

- Phase 2 serves one client at a time. Tests for simultaneous clients (T36, T37, T39 and the socket
  versions of T10, T11, T22, T23, T38) stay pending for Phase 3.
- If a non-host player were the only one still connected after the host's reconnect window
  expired, the game would continue without a host; this cannot happen with one client and is left for
  Phase 3 to decide.
- Fast-timer tests (question 0.6 s, intermission 0.2 s, final 0.3 s, reconnect 1 s) are used by
  `make test` so it finishes in about 25 s; the default-timer script measures the real values.
- The "red" step of each chunk was observed as a compile or link failure (the module did not exist
  yet) or a failing check before the implementation was written.

## Gate 3 review

An independent review of the whole diff found two game-logic defects, both fixed with a test that failed before the fix:
a player joining during an intermission now receives the next question, and a correct answer still scores if the player
disconnects before the round closes. The unit test count rose from 109 to 110 with the fix. The default-timer and
ten-run results above were collected before this fix, which does not touch timing; `make test` was rerun after it and passes.
