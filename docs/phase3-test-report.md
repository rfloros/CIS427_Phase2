# TermTrivia Phase 3 test report

## 1. Identification and context

| Item | Value |
|---|---|
| Project | CIS 427 Socket Programming Project, Phase 3 (many clients, many games) |
| Application | TermTrivia: terminal multiplayer trivia over TCP |
| Team | Robert, Matthew |
| Build tested | branch `agentloop/20261029-1650-termtrivia-phase3`, based on `main` at `0f9acd0`; the final commit is recorded in the Gate 3 report |
| Protocol version | Revision 2 plus the Phase 2 clarifications and Phase 3 decisions 38 to 44 (`docs/design-changes.md`) |
| Test plan | `docs/test-plan.md` (T01 to T39, plus T40 to T47 added in Phase 3) |
| Date of the runs | 2026-10-29 |
| Who ran the tests | The AI coding assistant (Claude) that wrote the Phase 3 code, on Robert's machine. The team has not yet re-run them. |

## 2. Test environment

| Item | Value |
|---|---|
| OS | Omarchy (Arch-based), Linux 7.2.5-3-omarchy x86_64 |
| Compiler | g++ (GCC) 16.2.1 20260810, `-std=c++17 -Wall -Wextra -pthread`, no warnings |
| Hardware | personal computer, 8 logical CPUs, 15 GiB RAM |
| Network | everything on one machine over localhost (127.0.0.1) |
| Dependencies | none beyond the C++ standard library and POSIX sockets; test framework `tests/minitest.h` |

## 3. Scope and approach

Everything ran on one machine with localhost sockets, short timers for `make test` (question 0.6 s, intermission
0.2 s, reconnect 1 s unless a test says otherwise), and the real 20/5/10/60 s timers for the default-timer run and
the 20-player measurement. Simultaneous clients are threads or processes on the same machine, so true network delay,
packet loss, and several machines are **not** covered.

| Layer | Tool | What it covers in Phase 3 |
|---|---|---|
| Unit tests | `build/unit_tests` | game logic with a fake clock: host expiry, expiry versus reconnect, final-intermission join, protocol text |
| Real-socket tests | `build/integration_tests` | the real server with several simultaneous clients (`tests/integration/test_concurrency.cpp`) and the real `Client` class |
| Scenario scripts | `tests/scripts/*.sh` | failure scenarios, deliverable checks (`check_deliverables.sh`) |
| Default timers | `make test-default-timers` | the real timers |
| Measurement | `scripts/measure_nfr.sh` | 20 clients with the real timers |
| Race detector | ThreadSanitizer build of the integration tests | data races in the server threads |

## 4. Summary of results

| Measure | Result |
|---|---|
| New or changed Phase 3 socket tests | T10, T11, T22, T23, T35 to T47 (see section 5) |
| Each new socket test repeated 10 times | 150 of 150 runs passed |
| `make test` | unit: 116 tests run, 0 check failures; integration: 75 tests run, 0 check failures; scripts: all checks passed |
| `make test-default-timers` | all checks passed (real 20/5/10/60 s timers) |
| 20-player measurement (`scripts/measure_nfr.sh`) | replies 1 ms, question push 2 ms, round timer 20 006 ms (limits 500 ms, 1 s, 20 s plus or minus 1 s) |
| ThreadSanitizer, 75 integration tests | no report from any Phase 3 code; one report in a Phase 2 startup test (see section 6) |
| Cases failed | 0 open |

## 5. Phase 3 test cases and observations

Names in the first column are the test titles to filter on, for example `build/integration_tests T40`.

| ID | What ran | Observed |
|---|---|---|
| T10, T22, T23, T41 (lobby) | Host plus Bob, Carol, Dave join; Bob sends START_ROUND and END_GAME; a duplicate name joins; Carol QUITs | Each join got its own token; 402 for both host commands; 409 for the duplicate and that connection then joined as Dave; Carol got 200 and EOF; the host's leaderboard listed the remaining players only. Pass. |
| T11 | Host connected while a second client joined; both sent LEADERBOARD | Both stayed connected, got identical three-line replies. Pass. |
| T41 (mid-game and endings) | Join during a live question; host QUIT; END_GAME with three players | Late joiner got 200, then 420 on an answer, then QUESTION 2 and could answer. Host QUIT: the host saw exactly `200 OK` and a blank line then EOF; other players saw `GAME_END HOST_QUIT` and EOF. END_GAME: host and both players saw `HOST_ENDED` and EOF. Pass. |
| T35 (with a player) | 30 games with two connected players; START_ROUND | In all 30 the 200 OK came before QUESTION 1, and the guest also got QUESTION 1. Pass. |
| T36 | Host, Bob, Carol answer at one barrier-released instant, 5 times | Each client got one `200 OK`; ROUND_RESULT showed Robert:1, Carol:1, Bob:0 each time. Pass. |
| T37 | Two clients JOIN_GAME with the same name at once, 10 times | In every run one got 200 and one 409; the leaderboard held one copy of the name. Pass. |
| T38 | Two games; END_GAME in game A while game B had an answered question | Game A's players got `HOST_ENDED` and EOF. Game B kept running; its second answer scored and ROUND_RESULT showed Ben:1, Bea:1. Pass. |
| T39 | Three clients sent 100 LEADERBOARD requests each while a 5-question game pushed results | Every message on the host connection was complete and ended with a blank line; no status or PUSH line appeared inside another message. Pass. |
| T40 | 40 long-named players made each leaderboard reply about 1 KB; three clients flooded LEADERBOARD without reading until the server stopped consuming their input; game B's host then sent LEADERBOARD 12 times | Every game B reply arrived in under 500 ms. On the earlier global-lock design the same test failed in 5 of 6 runs, which is why the lane design was adopted. Pass. |
| T42 (lobby, socket) | Host killed in a lobby with one player; reconnect window 1 s | Player received exactly `PUSH GAME_END HOST_EXPIRED`, `FINAL RESULTS`, `Bob:0`, then EOF. Old host and player tokens got 410. Log contained `ended: HOST_EXPIRED`. Pass. |
| T42 (all pending) | Host and the only player both killed in a lobby | After the window both old tokens got 410 and a new JOIN_GAME got 404. Pass. |
| T42 (unit) | Expiry versus reconnect; lobby expiry | A reconnect inside the window won; one after it lost and the lobby ended. Pass. |
| T43 | Join during the final intermission | 200 OK, no question, then `GAME_END COMPLETE` listing `Robert:1` and `Late:0`, then EOF. Pass. |
| T44 | Stop with idle and playing clients; 12 game cycles and 12 quick connections; injected handler and timer launch failures; join racing a rolled-back creation | Stop finished in under 1.5 s with exactly one server shutdown log line; worker count stayed at 4 or fewer after the cycles; a failed handler launch closed that client and the next client was served; a failed timer launch gave no 200, left no game, and was logged; the racing JOIN_GAME got 404. Pass. |
| T45 | Three idle connections then a fourth creating a game; 20 players | The fourth client got its reply well under 500 ms. With 20 players the leaderboard, START_ROUND reply, and every question push met the limits in the test; the real-timer measurement is in section 6. Pass. |
| T46 | A player was killed after answering, a round closed, the player reconnected | Reconnect gave `200 OK`, `1 1`; the leaderboard showed Robert:1 and Bob:1; the host's play was unaffected. Pass. |
| T47 | Host killed during play, reconnect window 0.5 s, 3 questions (socket) and a 2-question game with a 30 s window (unit) | The game ran to `GAME_END COMPLETE` listing only the remaining player; nobody received `HOST_EXPIRED`; the other player got 402 for START_ROUND and END_GAME; the old host token got 410. Pass. |
| Late replies (found in testing) | A command read just before its game ended | An early version could write a reply between `GAME_END` and the close. Fixed (design-changes decision 44), regression test added; 8 repeats of the racing test passed afterwards. |

## 6. Measurements, race detection, and limitations

**NFR-01 with 20 localhost clients and the real timers** (`bash scripts/measure_nfr.sh`, run twice):

| Measure | Limit | Run 1 | Run 2 |
|---|---|---|---|
| LEADERBOARD reply (20 players in the game) | 500 ms | 1 ms | 1 ms |
| START_ROUND reply | 500 ms | 1 ms | 1 ms |
| START_ROUND sent to question 1 seen by the slowest of 20 clients | 1000 ms | 2 ms | 2 ms |
| Question 1 to its ROUND_RESULT | 20 000 ms plus or minus 1000 | 20 001 ms | 20 006 ms |

Client clocks are those of separate processes started within a few milliseconds of each other, so push times are
accurate to about 10 ms, not exact.

**ThreadSanitizer.** The 75 integration tests were built with `-fsanitize=thread` and run once. It produced no
report for the server's handler, timer, lane, routing, or delivery code. It produced one report in the Phase 2 test
`Slice5 ...` that stops a server started through `run_server` by signal-style stop while the server object is being
destroyed (`Server::request_stop` writes to the wake-up pipe while `~Server` closes it). It is in the startup path,
not in a code path changed for Phase 3, and it was not changed.

**Limitations.**

- One machine and localhost only; no packet loss, latency, or several machines.
- At most 20 players in one game and a handful of games, not hundreds of clients.
- Timing assertions use generous bounds on a lightly loaded machine, so a heavily loaded machine could see
  occasional timing failures.
- The tests were run by the assistant that wrote the code, on one machine. The team should re-run
  `make clean && make test` before the demonstration and record its own results.
- `T40` depends on the kernel's socket buffer sizes; on a different kernel the fixture may need larger floods.
- The team's contribution summary and Ripple reflection are written by the team (Matthew's individual reflection is
  still pending) and are not part of these results.

## 7. How to reproduce

```sh
make clean && make all          # no warnings
make test                       # unit, socket, and script tests (about 35 s)
make test-default-timers        # real timers (about 2.5 minutes)
bash scripts/measure_nfr.sh     # 20-player timing (about 35 s)
bash scripts/package-phase3.sh --verify-build   # draft zip, extracted, built, and tested
```
