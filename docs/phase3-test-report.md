# TermTrivia Phase 3 test report

## 1. Identification and context

| Item | Value |
|---|---|
| Project | CIS 427 Socket Programming Project, Phase 3 (many clients, many games) |
| Application | TermTrivia: terminal multiplayer trivia over TCP |
| Team | Robert, Matthew |
| Build tested | developed on branch `agentloop/20261029-1650-termtrivia-phase3` and merged to `main` (commit `45ad293`); T51, T52, and the team's re-run use later commits on `main` that changed only tests and documents |
| Protocol version | Revision 2 plus the Phase 2 clarifications and Phase 3 decisions 38 to 44 (`docs/design-changes.md`) |
| Test plan | `docs/test-plan.md` (T01 to T39, plus T40 to T47 added in Phase 3, and T51 restart and T52 load added on 2026-11-09) |
| Date of the runs | 2026-10-29; T51 and T52 on 2026-11-09 |
| Who ran the tests | The AI coding assistant (Claude) that wrote the Phase 3 code, on Robert's machine. T51 and T52 were run by the same assistant on a cloud VM (section 2). On 2026-11-09 Robert re-ran `make clean && make test` on his own Linux machine; it finished with all checks passed (116 unit tests, 75 socket tests, and every script check, including T51). The team has not re-run the 20-player measurement, the default-timer run, or the load test (T52); those results are the assistant's. |

## 2. Test environment

| Item | Value |
|---|---|
| OS | Omarchy (Arch-based), Linux 7.2.5-3-omarchy x86_64 |
| Compiler | g++ (GCC) 16.2.1 20260810, `-std=c++17 -Wall -Wextra -pthread`, no warnings |
| Hardware | personal computer, 8 logical CPUs, 15 GiB RAM |
| Network | everything on one machine over localhost (127.0.0.1) |
| Dependencies | none beyond the C++ standard library and POSIX sockets; test framework `tests/minitest.h` |

**T51 and T52 (2026-11-09)** were run on a cloud VM instead: Ubuntu 24.04.5 LTS, Linux 6.18.44 x86_64, g++ 13.3.0
with the same flags and no warnings, 2 CPUs, 7.8 GiB RAM, everything over localhost. Robert's own re-run of `make test` on 2026-11-09 included
T51 (`tests/scripts/restart.sh`); the load test (T52) has not been re-run on the team's machine.

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
| Restart behavior (T51, `tests/scripts/restart.sh`) | 18 of 18 checks passed: a clean stop and a crash, each followed by a restart on the same port |
| Load test (T52, `scripts/stress_test.sh`) | 55 clients, 11 000 of 11 000 requests succeeded; 210 clients, 21 000 of 21 000 succeeded (section 6) |
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
| T51 | A game with a host and a player was running; the server was stopped with SIGINT, started again on the same port, and the whole sequence was repeated with SIGKILL | In both cases the host and player connections were closed and the server started again on the same port. The old host token and the old player token each got 410, the old join code got 404, and a new CREATE_GAME on the new server got 200 OK. The clean stop logged `server shutting down`, and the log showed three server starts. A mutated copy of the script that expected 200 instead of 410 and 404 failed six checks, so the checks can fail. Pass. |
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

**Load test (T52).** `bash scripts/stress_test.sh [games] [players per game] [requests per client]`. Every client
connects, joins its game, and then sends its requests one at a time, waiting for each reply. Nine in ten requests are
LEADERBOARD (expected reply `200 OK`) and one in ten is an invalid `HELLO` (expected reply `300 message format error`),
so the run also shows that bad input does not disturb the other clients. The games stay in the lobby, so no timed
pushes arrive between a request and its reply. A request counts as failed if its reply is wrong or does not arrive
within 5 seconds.

| Measure | 5 games, run 1 | 5 games, run 2 | 10 games |
|---|---|---|---|
| Clients (a host and 10 or 20 players per game) | 55 | 55 | 210 |
| Requests per client | 200 | 200 | 100 |
| Requests sent | 11 000 | 11 000 | 21 000 |
| Successful requests | 11 000 | 11 000 | 21 000 |
| Failed requests | 0 | 0 | 0 |
| Test duration | 9.9 s | 9.4 s | 22.5 s |
| Throughput | 1 109 requests/s | 1 170 requests/s | 932 requests/s |
| Response time, average | 36.4 ms | 35.5 ms | 154.5 ms |
| Response time, median | 35.2 ms | 34.6 ms | 139.9 ms |
| Response time, 95th percentile | 60.3 ms | 57.8 ms | 326.0 ms |
| Response time, slowest | 741.0 ms | 234.2 ms | 1 207.6 ms |
| Server threads at peak | 61 | 61 | 221 |
| Server memory (RSS) at peak | 5 MiB | 5 MiB | 8 MiB |
| Server alive afterwards, SIGINT exit status 0, shutdown logged, no ERROR lines | yes | yes | yes |

**What the load test showed and its limits.**

- No request failed and the server never crashed. After the run SIGINT stopped it cleanly.
- Thread counts equal the clients plus the games plus one (61 = 55 + 5 + 1; 221 = 210 + 10 + 1), which matches the
  design of one thread per client and one timer thread per game. Memory stayed small, but threads grow with clients.
- Response times rose from about 35 ms (55 clients) to about 155 ms (210 clients), and throughput stopped growing at
  roughly 1 000 requests per second. The test clients are bash processes on the same 2-CPU machine as the server, so
  these times include the clients' own overhead and are much higher than the 1 ms reply time measured with 20 players
  on an 8-CPU machine (above). Treat them as a comparison between runs, not as the server's speed.
- In the 210-client run the slowest reply took 1.2 s, which is above the 500 ms limit in NFR-01. NFR-01 applies to up
  to 20 players on a lightly loaded machine, and this run used 210 clients competing for 2 CPUs, so it is not a failure
  of that requirement. It does show where the thread-per-client design starts to slow down.
- The games stayed in the lobby, so the load test did not exercise timed pushes or answers. Those are covered by T36,
  T39, T40, and the 20-player measurement.
- It ran on one cloud VM over localhost, so there was no network delay or packet loss.

**Limitations.**

- One machine and localhost only; no packet loss, latency, or several machines.
- Timed play was tested with at most 20 players in one game and a handful of games. The load test reached 210
  clients in 10 games, but only in the lobby and with a bash client on a 2-CPU machine.
- Timing assertions use generous bounds on a lightly loaded machine, so a heavily loaded machine could see
  occasional timing failures.
- Most results were produced by the assistant that wrote the code. Robert re-ran `make clean && make test` on his
  own machine on 2026-11-09 and it passed. The 20-player measurement, the default-timer run, and the load test have
  not been re-run by the team.
- `T40` depends on the kernel's socket buffer sizes; on a different kernel the fixture may need larger floods.
- The team's contribution summary and Ripple reflection are written by the team and are not part of these
  results.

## 7. Phase 1 and Phase 2 test cases (T01 to T39), final results

These are the Phase 1 test plan and the Phase 2 results, rerun as part of `make test`. The full text of each test is
in `docs/test-plan.md`, and the Phase 2 run itself is in `docs/phase2-test-report.md`. The Pass column is the final
result after Phase 3: every case now passes with real sockets. T36, T37, and T39 were not run in Phase 2 because they
need several clients; T10, T11, T22, T23, and T38 passed at game-logic level in Phase 2 and passed with sockets in
Phase 3.

| Test ID | Requirement tested | Final result |
|---|---|---|
| T01 | FR-01 Create a game | Pass |
| T02 | FR-03, FR-04, NFR-01 Start the game and receive the first question | Pass |
| T03 | FR-05, FR-07 Submit an answer and close the round early | Pass |
| T04 | FR-07, NFR-01 Round closes on the timer | Pass |
| T05 | FR-06, NFR-10 Scoring ignores speed | Pass |
| T06 | FR-08 View the leaderboard | Pass |
| T07 | FR-09 Game ends automatically | Pass |
| T08 | FR-10 Host ends the game early | Pass |
| T09 | FR-11 Host quits | Pass |
| T10 | FR-11 Non-host quits (Phase 3) | Pass (logic and sockets) |
| T11 | FR-02 Join a game in the lobby (Phase 3) | Pass (logic and sockets) |
| T12 | FR-20, NFR-05, NFR-06 Client shows commands and input formats | Pass |
| T13 | FR-25, NFR-09 Server log contents | Pass |
| T14 | FR-18 Empty line | Pass |
| T15 | FR-18 Unknown command | Pass |
| T16 | FR-14 Bad CREATE_GAME arguments | Pass |
| T17 | FR-13 Unknown join code | Pass |
| T18 | FR-16 Command that needs a game | Pass |
| T19 | FR-19 Command in the wrong game state | Pass |
| T20 | FR-18 Invalid answer choice | Pass |
| T21 | FR-18, NFR-09 Over-length line | Pass |
| T22 | FR-17 Non-host uses a host command (Phase 3) | Pass (logic and sockets) |
| T23 | FR-15 Duplicate name in a game (Phase 3) | Pass (logic and sockets) |
| T24 | FR-20 Server not running | Pass |
| T25 | FR-20 Invalid port | Pass |
| T26 | FR-21 Client exits unexpectedly | Pass |
| T27 | FR-12, NFR-03 Reconnect within the window | Pass |
| T28 | FR-12, NFR-03, NFR-09 Reconnect rejected | Pass |
| T29 | FR-20 Server stops while the client is active | Pass |
| T30 | FR-22 Host disconnect does not end the game | Pass |
| T31 | FR-24, NFR-04 Command split across reads | Pass |
| T32 | FR-24, NFR-04 Two commands in one send | Pass |
| T33 | FR-24, NFR-04 One byte at a time | Pass |
| T34 | FR-04, FR-24 Client reassembles split server messages | Pass |
| T35 | FR-03, FR-24 Reply written before pushes | Pass |
| T36 | FR-23 Simultaneous answers | Pass |
| T37 | FR-15 Simultaneous joins with the same name | Pass |
| T38 | FR-23 Games are isolated | Pass (logic and sockets) |
| T39 | NFR-07, FR-24 Messages are never interleaved | Pass |

## 8. How to reproduce

```sh
make clean && make all          # no warnings
make test                       # unit, socket, and script tests, including T51 (about 35 s)
make test-default-timers        # real timers (about 2.5 minutes)
bash scripts/measure_nfr.sh     # 20-player timing (about 35 s)
bash scripts/stress_test.sh     # load test, T52 (about 10 s; arguments: games, players per game, requests)
bash tests/scripts/restart.sh   # restart behavior, T51 (runs as part of make test)
bash scripts/package-phase3.sh --verify-build   # submission zip, extracted, built, and tested
```
