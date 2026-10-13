# TermTrivia Phase 2 test report

## 1. Identification and context

| Item | Value |
|---|---|
| Project | CIS 427 Socket Programming Project, Phase 2 (one client, one server) |
| Application | TermTrivia: terminal multiplayer trivia over TCP |
| Team | Robert, Matthew |
| Build tested | git commit `8750e774e46c87e9d72d6068c5a390e79bc84a7f` on branch `main` (repository `rfloros/CIS427_Phase2`) |
| Protocol version | Revision 2 (`docs/protocol.md`), plus the clarifications in `docs/design-change-summary.md` |
| Test plan | `docs/test-plan.md` (39 tests, T01 to T39) |
| Date of the runs below | 2026-10-13 (both verification runs); earlier development runs 2026-10-12 |
| Who ran the tests | Automated suites written with the code. Verification run 1 (cloud VM, 2026-10-13): Claude (AI assistant) at Robert's request. Verification run 2 (Robert's own machine, 2026-10-13): Robert. Both runs used the commands in section 8. |

## 2. Test environment

| Item | Cloud VM run | Robert's machine |
|---|---|---|
| OS | Ubuntu 24.04.5 LTS, Linux 6.18.44 x86_64 | Omarchy 4.0.4 (Arch-based), Linux 7.2.5-3-omarchy x86_64 |
| Compiler | g++ 13.3.0, `-std=c++17 -Wall -Wextra -pthread`, no warnings | g++ (GCC) 16.2.1 20260810, same flags |
| Hardware | cloud VM, 2 CPUs, 7.8 GiB RAM | Personal computer, 8 logical CPUs, 15 GiB RAM |
| Network | everything on one machine over localhost (127.0.0.1); no separate machines | Same: one machine, localhost (127.0.0.1) |
| Dependencies | none beyond the C++ standard library and POSIX sockets; test framework is our own `tests/minitest.h` | Same |

## 3. Scope and approach

**In scope (Phase 2):** one client at a time talking to one server. Connection setup and teardown; all eight commands (CREATE_GAME, JOIN_GAME, START_ROUND, SUBMIT_ANSWER, LEADERBOARD, RECONNECT, QUIT, END_GAME) with their exact response codes (200, 300, 401, 402, 404, 409, 410, 420, 421, 422, 423); malformed and unexpected input; message framing; unexpected disconnects and reconnecting; timers; clean shutdown with Ctrl+C; server log contents.

**Not in scope (Phase 3):** several clients at the same time. Tests T36, T37 and T39 need that and are not run. T10, T11, T22, T23 and T38 involve a second player. Their game logic is tested directly in unit tests; their two-client socket versions are left for Phase 3.

**How it was tested, all automated:**

| Layer | Tool | What it does |
|---|---|---|
| Unit tests | `build/unit_tests` | framing, protocol parser and builders, question bank, game logic driven by a fake clock, logging, configuration |
| Real-socket tests | `build/integration_tests` | the real server over TCP with short timers, the real `Client` class against a fake server, startup failures |
| Scenario scripts | `tests/scripts/scenarios.sh` | the real `server` and `client` binaries: bad input, split writes, no server, bad ports, killed client then reconnect, server stopped, Ctrl+C, tokens never logged |
| Default-timer run | `make test-default-timers` | the real 20 / 5 / 10 / 60 second timers, measured, about 2 minutes |

`make test` uses short timers (question 0.6 s, intermission 0.2 s) so it finishes in about 25 seconds. Only the default-timer run proves the spec timings.
Raw bytes for the framing tests are sent by a small helper, `build/raw_client`, so messages can be split at chosen points.

## 4. Summary of results

| Measure | Count |
|---|---|
| Planned test cases | 39 |
| Phase 2 cases passed with real sockets | 31 (T01 to T09, T12 to T21, T24 to T35) |
| Cases passed at game-logic level (second player needed for the socket test) | 5 (T10, T11, T22, T23, T38) |
| Cases failed | 0 |
| Cases not run (Phase 3, several clients) | 3 (T36, T37, T39) |
| Automated checks | unit: 110 tests run, 0 check failures; integration: 51 tests run, 0 check failures; scripts: all checks passed (199 PASS lines in `make test`); default timers: all checks passed |

**Verdict: acceptable for Phase 2.** Every Phase 2 case passes, every timing meets the spec, and no failure remains open. The only gaps are the Phase 3 multi-client tests, which the assignment does not require now.

## 5. Test cases

Expected results come from `docs/protocol.md` (revision 2), `docs/requirements.md` and `docs/test-plan.md`. The same table with the full setup text is in `docs/test-plan.md`.

### Normal operation (connection, every command, clean shutdown)

| ID | Requirement | Steps | Expected (from the spec) | Actual | Result |
|---|---|---|---|---|---|
| T01 | FR-01 Create a game | Server running. Client connected. Not in a game. CREATE_GAME Robert 3 | 200 OK, then a 4-character join code, then a 4-character token, then a blank line. The client shows the join code and does not show the token. | 200 OK, 4-character code, 4-character token, blank line (unit T01, socket T01 T02). Client printed `Game created. Join code: XXXX`, never the token, and wrote a 0600 session file (client test T01, scenarios.sh). | Pass |
| T02 | FR-03, FR-04, NFR-01 Start the game and receive the first question | Host has created a game with 3 questions. START_ROUND | The 200 OK reply arrives first. Then PUSH QUESTION 1 with options A to D is displayed within 1 second, with no user input. | 200 OK arrived first, then PUSH QUESTION 1 with options A to D, with no input. Default timers: reply 1 ms, first question 1 ms after START_ROUND (`make test-default-timers`). | Pass |
| T03 | FR-05, FR-07 Submit an answer and close the round early | Question 1 is open. The host is the only active player. SUBMIT_ANSWER B | 200 OK. Because every active player has answered, PUSH ROUND_RESULT 1 follows at once with the correct answer and the leaderboard, without waiting 20 seconds. | 200 OK, then PUSH ROUND_RESULT 1 with the correct answer and leaderboard in under 0.4 s with a 0.6 s timer (socket T03; unit T03). | Pass |
| T04 | FR-07, NFR-01 Round closes on the timer | A question is open. No answer is submitted. Wait | PUSH ROUND_RESULT arrives 20 seconds (plus or minus 1) after the question, with the score unchanged. The next PUSH QUESTION arrives 5 seconds (plus or minus 1) later. | Default timers: ROUND_RESULT 20016 ms after the question (target 20000 +/- 1000), next QUESTION 5020 ms later (target 5000 +/- 1000), score unchanged (`make test-default-timers`). Fast-timer socket test T04 also passes. | Pass |
| T05 | FR-06, NFR-10 Scoring ignores speed | Two separate games, each with a question open. In one game answer correctly after 1 second. In the other answer correctly after 18 seconds. | In both games the player gains exactly 1 point. | Logic test: a correct answer after 1 s and after 18 s each gives exactly 1 point; wrong answer gives 0 (unit T05). | Pass |
| T06 | FR-08 View the leaderboard | In a game, after at least one round has closed. LEADERBOARD | 200 OK, then one name:score line per player sorted highest first, then a blank line. | 200 OK then `name:score` lines, highest first, blank line (unit T06, socket T06). | Pass |
| T07 | FR-09 Game ends automatically | The final question's round has just closed. Wait | After a 10-second intermission, PUSH GAME_END COMPLETE with FINAL RESULTS arrives and the connection closes. The server keeps listening. | Default timers: GAME_END COMPLETE 10003 ms after the final result (target 10000 +/- 1000); connection closed; a new client could create a game afterwards (socket T07, `make test-default-timers`). | Pass |
| T08 | FR-10 Host ends the game early | Game running with a question open. END_GAME | 200 OK, then PUSH GAME_END HOST_ENDED, then the connection closes. No further QUESTION pushes arrive. The server accepts new connections. | 200 OK, PUSH GAME_END HOST_ENDED, close; no more QUESTION pushes; server accepted a new client (socket T08, unit T08). | Pass |
| T09 | FR-11 Host quits | Host is in a game. QUIT | 200 OK, then the connection closes and the game is removed. Any other players receive PUSH GAME_END HOST_QUIT. RECONNECT with the old token returns 410. | Host wire sequence was exactly `200 OK`, blank line, EOF (no GAME_END to the host); RECONNECT with the old token returned 410; other players get HOST_QUIT in the logic test (socket T09, unit T09). | Pass |
| T10 | FR-11 Non-host quits (Phase 3) | Host and one player are in a game. The player sends QUIT | The player gets 200 OK and is disconnected. The game continues for the host. The player no longer appears in LEADERBOARD. | Logic level: non-host QUIT returns 200 + close, game continues, player gone from LEADERBOARD (unit T10). Socket test with two clients is Phase 3. | Logic: Pass. Sockets: Pending (Phase 3) |
| T11 | FR-02 Join a game in the lobby (Phase 3) | A game is in LOBBY. A second client is connected. JOIN_GAME <code> Matthew | 200 OK, then '2 players in game <code>', then a token, then a blank line. | Logic level: lobby join returns `2 players in game <code>` and a 4-character token (unit T11). Socket test with two clients is Phase 3. | Logic: Pass. Sockets: Pending (Phase 3) |
| T12 | FR-20, NFR-05, NFR-06 Client shows commands and input formats | Client started with a valid host and port. Observe the startup screen and each prompt. Enter an invalid menu choice. | All commands are listed on connect. Each prompt shows its valid input format. The invalid choice gives a message and a new prompt, not an exit. | Client printed all eight commands plus `help` on connect and a prompt line with every format; an invalid choice printed `Unknown command` or the usage line and a new prompt, with nothing sent (client tests T12). | Pass |
| T13 | FR-25, NFR-09 Server log contents | Server started. Tests above run in one session. Read server.log | Timestamped lines for startup, listening address and port, connect (with client id), each request type and outcome, errors, disconnect, and shutdown. No token appears anywhere. | Log had timestamped startup, listening address and port, connect with client id, each request with outcome, protocol errors, disconnect, shutdown. The token never appeared (unit T13, socket T13, scenarios.sh, `docs/sample_server.log`). | Pass |

### Invalid and malformed input

| ID | Requirement | Steps | Expected (from the spec) | Actual | Result |
|---|---|---|---|---|---|
| T14 | FR-18 Empty line | Client connected. Send a blank line | 300 message format error. The connection stays open and the next valid command works. | 300 message format error; connection stayed open and the next command worked (unit, socket T14, scenarios.sh). | Pass |
| T15 | FR-18 Unknown command | Client connected. HELLO | 300 message format error. The connection stays open. | 300 message format error; connection stayed open (unit, socket T15, scenarios.sh). | Pass |
| T16 | FR-14 Bad CREATE_GAME arguments | Not in a game. The server has N questions loaded. CREATE_GAME Robert abc, then CREATE_GAME Robert 0, then CREATE_GAME Robert 9999, then CREATE_GAME | The first three return 423 with the range 1 to N. The last returns 300. No game is created. | `abc`, `0`, `9999` gave `423 ... between 1 and N`; bare CREATE_GAME gave 300; no game was created (unit, socket T16, scenarios.sh). | Pass |
| T17 | FR-13 Unknown join code | Client connected. Not in a game. JOIN_GAME ZZZZ Matthew | 404 No game exists with that code. The client can try again. | 404 No game exists with that code; the client could try again (unit T17, socket T17). | Pass |
| T18 | FR-16 Command that needs a game | Client connected. Not in a game. LEADERBOARD, then SUBMIT_ANSWER A, then START_ROUND | Each returns 401 You are not currently in a game, join or reconnect first. | LEADERBOARD, SUBMIT_ANSWER A, START_ROUND each returned 401 (unit T18, socket T18). | Pass |
| T19 | FR-19 Command in the wrong game state | Host in a game that is still in LOBBY. SUBMIT_ANSWER A; then START_ROUND twice; then SUBMIT_ANSWER twice in one round | 420 for the first answer. 421 for the second START_ROUND. 422 for the second answer in the round. | 420 for the lobby answer, 421 for the second START_ROUND, 422 for the second answer in a round (unit T19, socket T19). | Pass |
| T20 | FR-18 Invalid answer choice | A question is open. SUBMIT_ANSWER E | 300 message format error. The player can still submit A to D afterwards. | `SUBMIT_ANSWER E` returned 300 and A to D still worked afterwards (unit T20, socket T17 T18 T19 T20). | Pass |
| T21 | FR-18, NFR-09 Over-length line | Client connected. Send one line of 600 characters, then LEADERBOARD | 300 for the long line. Input up to its newline is discarded. The next command is handled normally. No crash. | 600 characters gave one 300; input to the newline was discarded; LEADERBOARD afterwards was handled normally; no crash (unit T21, socket T21, scenarios.sh). | Pass |
| T22 | FR-17 Non-host uses a host command (Phase 3) | Host and a player are in a game. The player sends END_GAME | 402 Only the host may execute this command. The game continues. | Logic level: non-host START_ROUND and END_GAME return 402 and the game continues (unit T22). Socket test with two clients is Phase 3. | Logic: Pass. Sockets: Pending (Phase 3) |
| T23 | FR-15 Duplicate name in a game (Phase 3) | A game already has a player named Matthew. Another client sends JOIN_GAME <code> Matthew | 409 That name is already in use in this game. The client can choose another name. | Logic level: duplicate name returns 409 (names are case-sensitive) (unit T23). Socket test with two clients is Phase 3. | Logic: Pass. Sockets: Pending (Phase 3) |

### Networking failures and teardown

| ID | Requirement | Steps | Expected (from the spec) | Actual | Result |
|---|---|---|---|---|---|
| T24 | FR-20 Server not running | No server is listening. Start the client with a host and port | A clear message such as 'Could not connect to host:port', exit with a non-zero status, no unhandled exception. | `Could not connect to 127.0.0.1:<port>` on stderr and a non-zero exit status, no exception (scenarios.sh). | Pass |
| T25 | FR-20 Invalid port | Client not started. Start the client with port abc, then 0, then 70000 | An error or usage message for each. The client exits without attempting a connection. | `abc`, `0`, `70000` each printed `invalid port` and a usage line and exited non-zero; the server log showed no connection attempt (scenarios.sh). | Pass |
| T26 | FR-21 Client exits unexpectedly | Client is in a running game. Kill the client process | The server logs the disconnect and keeps listening. The player becomes DISCONNECTED_PENDING for 60 seconds. No crash. | Killed the real client with SIGKILL: the server logged the disconnect, kept listening, and kept the player as DISCONNECTED_PENDING (scenarios.sh, socket T26 T27). | Pass |
| T27 | FR-12, NFR-03 Reconnect within the window | After the previous test, less than 60 seconds have passed. RECONNECT Robert <code> <token> from a new client | 200 OK, then '<score> <question number>'. If a round is live, the current question is pushed again. The score is unchanged. | RECONNECT with the saved session returned 200 and `<score> <question>`; the live question was pushed again; score unchanged (scenarios.sh, socket T26 T27, unit T27). | Pass |
| T28 | FR-12, NFR-03, NFR-09 Reconnect rejected | A pending session exists. RECONNECT with (a) a wrong token, (b) a wrong name, (c) correct details after 60 seconds | All three return the identical 410 reply. | Wrong token, wrong name, and correct details after the window all returned the identical 410 line. After the real 60 s window: 410 (`make test-default-timers`; socket T28 uses a 1 s window). | Pass |
| T29 | FR-20 Server stops while the client is active | Client is waiting at its prompt. Stop the server | The client prints that the server closed the connection and exits cleanly. No crash. | With the server killed, the client printed `Server closed the connection.` and exited with status 0 (scenarios.sh, client tests T29). | Pass |
| T30 | FR-22 Host disconnect does not end the game | Game running. Kill the host client, wait 25 seconds, then RECONNECT the host | The game keeps running. The host reconnects with host powers. END_GAME is still accepted. | Host killed, reconnected 25 s later with default timers: 200 OK, game kept running, END_GAME accepted (`make test-default-timers`, socket T30, unit T30). | Pass |

### Framing (partial reads, split and joined messages)

| ID | Requirement | Steps | Expected (from the spec) | Actual | Result |
|---|---|---|---|---|---|
| T31 | FR-24, NFR-04 Command split across reads | Scripted raw-socket client connected. Send 'CREATE_GAME Rob', wait 500 ms, then send 'ert 3' and a newline | Exactly one 200 OK reply for CREATE_GAME Robert 3. No 300 error. | Split `CREATE_GAME Rob` + `ert 3` gave exactly one 200 reply and no 300 (unit T31, socket T31, scenarios.sh). | Pass |
| T32 | FR-24, NFR-04 Two commands in one send | In a game. Scripted client. Send 'LEADERBOARD' newline 'LEADERBOARD' newline in a single send | Two complete replies, in order, each ending with a blank line. | Two LEADERBOARD commands in one send gave two complete replies in order (unit T32, socket T32, scenarios.sh). | Pass |
| T33 | FR-24, NFR-04 One byte at a time | Scripted client connected. Send CREATE_GAME Robert 3 and a newline one byte per send, 10 ms apart | The same reply as the create test. | CREATE_GAME sent one byte at a time with 10 ms gaps gave the same 200 reply (unit T33, socket T33, scenarios.sh). | Pass |
| T34 | FR-04, FR-24 Client reassembles split server messages | A test server sends bytes in awkward chunks: 1 byte at a time, a reply and a push together, a push between a command and its reply. The client sends a command | Each push is printed once and in full. The reply goes back to the prompt. No push is ever treated as the command's reply. | Fake server sent 1 byte at a time, a reply and a push in one chunk, and a push before the reply: every push printed once and in full; the reply returned to the prompt (client tests T34). | Pass |
| T35 | FR-03, FR-24 Reply written before pushes | A game in LOBBY. START_ROUND, repeated 50 times on fresh games | In every run the 200 OK reply is received before PUSH QUESTION 1. | 50 fresh games: the 200 OK reply came before PUSH QUESTION 1 in all 50 runs (socket T35, repeated in 10 full suite runs). | Pass |

### Concurrency (Phase 3, several clients)

| ID | Requirement | Steps | Expected (from the spec) | Actual | Result |
|---|---|---|---|---|---|
| T36 | FR-23 Simultaneous answers | A question is open with 3 or more players. All players submit an answer at the same moment (scripted) | Every answer is counted once. Scores are correct. No crash. | Not run yet | Pending |
| T37 | FR-15 Simultaneous joins with the same name | A game in LOBBY. Two clients ready. Both send JOIN_GAME <code> Matthew at the same time | One gets 200 OK and the other gets 409. | Not run yet | Pending |
| T38 | FR-23 Games are isolated | Two games are running. The host of game A sends END_GAME | Game A ends. Game B continues unchanged. | Logic level: ending game A with END_GAME removes only game A; game B keeps its state and its player can still play (unit T38). Socket test with two simultaneous clients is Phase 3. | Logic: Pass. Sockets: Pending (Phase 3) |
| T39 | NFR-07, FR-24 Messages are never interleaved | Rounds open and close while clients send commands. Many clients send LEADERBOARD repeatedly during a running game | No client sees lines from two messages mixed together. Every message ends with a blank line. | Not run yet | Pending |

### Coverage of the required areas

| Required area | Tests |
|---|---|
| Connection setup and teardown | T01, T12, T24, T25, T26, T29 |
| CREATE_GAME | T01, T16 |
| JOIN_GAME | T11 (logic), T17, T23 (logic) |
| START_ROUND | T02, T19, T35 |
| SUBMIT_ANSWER | T03, T05, T19, T20 |
| LEADERBOARD | T06, T18 |
| RECONNECT | T27, T28, T30 |
| QUIT | T09, T10 (logic) |
| END_GAME | T08, T22 (logic) |
| Exact response codes | 200: T01, T03, T06, T08, T09; 300: T14, T15, T20, T21; 401: T18; 402: T22; 404: T17; 409: T23; 410: T28; 420 to 422: T19; 423: T16 |
| Malformed or unexpected input | T14 to T21, T31 to T34 |
| Clean shutdown | T07 (game end), T08, T09, T29, and the Ctrl+C checks in the scenario script |
| Multiple simultaneous clients | not required for Phase 2: T36, T37, T39 |

## 6. Evidence

### 6.1 Automated test output (cloud VM run and Robert's run gave the same result, 2026-10-13)

`make test`, summary lines:

```
110 tests run, 0 check failures
51 tests run, 0 check failures
== tests/scripts/scenarios.sh
all checks passed
```

Last lines of the scenario script (killed client, reconnect, Ctrl+C, token check):

```
PASS T26 unexpected disconnect marks the player pending and keeps the game
PASS T27 reconnect within the window restores score and repeats the live question
PASS T26 T27 killed client can reconnect inside the window and gets the live question
PASS T26 session file is private
PASS T26 server logs the disconnect
PASS T26 server keeps listening
PASS T27 reconnect works with the saved session
PASS T27 the live question is pushed again
PASS T27 leaderboard still shows the player
PASS T27 QUIT removes the session file
PASS SIGINT while blocked in accept exits 0
PASS SIGINT logs shutdown
PASS SIGINT while blocked in recv exits 0
PASS SIGINT sends no protocol push
PASS NFR-09 the token is a real value
PASS NFR-09 no token in the log
```

### 6.2 Timings with the default timers (`make test-default-timers`)

```
T02 START_ROUND reply latency:      1 ms (limit 500)
T02 first question push latency:    1 ms (limit 1000)
T04 question window:                20012 ms (target 20000 +/- 1000)
T04 intermission before question 2: 5005 ms (target 5000 +/- 1000)
T07 final question window:          20005 ms (target 20000 +/- 1000)
T07 final intermission:             10005 ms (target 10000 +/- 1000)
PASS T02 reply within 500 ms
PASS T02 first question within 1 s
PASS T04 question timer accurate
PASS T04 intermission timer accurate
PASS T07 final intermission accurate
PASS T04 score unchanged when nobody answers
waiting 25 seconds before reconnecting (T30)...
PASS T30 host reconnects after 25 s
PASS T30 host powers kept: END_GAME accepted
waiting 61 seconds for the reconnect window to expire (T28c)...
PASS T28c reconnect after 61 s is the generic 410
all checks passed
```

| Measurement | Spec | Cloud VM | Robert's machine |
|---|---|---|---|
| Reply to START_ROUND | under 500 ms | 1 ms | 0 ms |
| First question after START_ROUND | under 1 s | 1 ms | 0 ms |
| Question window | 20 s plus or minus 1 s | 20.012 s | 20.018 s |
| Intermission | 5 s plus or minus 1 s | 5.005 s | 5.020 s |
| Final question window | 20 s plus or minus 1 s | 20.005 s | 20.020 s |
| Final intermission | 10 s plus or minus 1 s | 10.005 s | 10.020 s |
| Reconnect 25 s after a disconnect | allowed | 200 OK, host powers kept | same |
| Reconnect 61 s after a disconnect | refused | 410 | same |

### 6.3 Server log sample (`docs/sample_server.log`)

The sample shows startup, listening address, connect, each request with its result code, the 300 and 423 rejections, a disconnect, a reconnect, a host QUIT, and shutdown. No token appears in it.

```
2026-10-12T20:11:08 INFO server starting
2026-10-12T20:11:08 INFO listening on 0.0.0.0:27304
2026-10-12T20:11:08 INFO client 1 connected from 127.0.0.1:60482
2026-10-12T20:11:08 INFO client 1 request CREATE_GAME -> 200
2026-10-12T20:11:08 INFO game 8YOS created by Robert with 3 questions
2026-10-12T20:11:08 INFO client 1 request START_ROUND -> 200
2026-10-12T20:11:08 INFO game 8YOS: question 1 opened
2026-10-12T20:11:08 WARN client 1 protocol error 300: rejected unknown request
2026-10-12T20:11:08 WARN client 1 protocol error 423: rejected CREATE_GAME request
2026-10-12T20:11:09 INFO client 1 request SUBMIT_ANSWER -> 200
2026-10-12T20:11:09 INFO game 8YOS: round 1 closed
2026-10-12T20:11:09 INFO client 1 request LEADERBOARD -> 200
2026-10-12T20:11:09 INFO game 8YOS: player Robert disconnected, reconnect window open
2026-10-12T20:11:09 INFO client 1 disconnected: closed by peer
2026-10-12T20:11:11 INFO client 2 connected from 127.0.0.1:60494
2026-10-12T20:11:11 INFO client 2 request RECONNECT -> 200
2026-10-12T20:11:11 INFO game 8YOS: player Robert reconnected
2026-10-12T20:11:11 INFO client 2 request LEADERBOARD -> 200
2026-10-12T20:11:11 INFO client 2 request QUIT -> 200
2026-10-12T20:11:11 INFO game 8YOS ended: HOST_QUIT
2026-10-12T20:11:11 INFO client 2 disconnected: session ended by server
2026-10-12T20:11:11 INFO server shutting down
```

### 6.4 Stress and race checks (inside `build/integration_tests`, results from the 2026-10-12 development runs; the suite passes again in the 2026-10-13 run)

| Check | Result |
|---|---|
| 50 fresh games: `200 OK` reply before `PUSH QUESTION 1` (T35) | 50 of 50 |
| 40 games where SUBMIT_ANSWER closes the round at once: reply before ROUND_RESULT | 40 of 40 |
| Up to 40 reconnects racing a round close | a QUESTION n never followed ROUND_RESULT n |
| 15 runs of LEADERBOARD spam against a timer-driven game end | complete GAME_END always arrived before EOF |
| 30 rapid games on fresh connections | first message on every connection was its own reply |
| Client that floods commands and never reads | dropped by the 200 ms send timeout, next client served |
| Ten repeated runs of `build/integration_tests` | 10 of 10 runs clean (51 tests each) |

## 7. Defects

An independent code review (Gate 3) of the whole change found two game-logic defects and one hardening gap. All were fixed, each with a test that failed before the fix. None is open.

| ID | Symptom | Exposed by | Severity | Cause and fix | Status |
|---|---|---|---|---|---|
| D-1 | A player who joined during an intermission waited and did not get the next question. | Gate 3 review; new unit test | Medium | Joiners stayed in the waiting state until a round was already open. They are now promoted before the next question is sent. | Fixed |
| D-2 | A correct answer was not scored if the player disconnected before the round closed. | Gate 3 review; new unit test | Medium | The answer was dropped with the disconnected player. It is now scored when the round closes. | Fixed |
| D-3 | No test proved that a client which never reads cannot block the server. | Gate 3 review; new integration test | Low (test gap) | The 200 ms send timeout already existed. A server-level test now floods a client that never reads and checks that it is dropped and the next client is served. | Fixed |

Known limits, not defects (see `docs/design-changes.md`):

- The server serves one client at a time in Phase 2. A second client waits in the listen backlog.
- If only non-host players were left after the host's reconnect window expired, the game would run without a host. This cannot happen with one client. It is for Phase 3 to decide.
- The default-timer numbers and the ten-run stability result were collected before the D-1 and D-2 fixes. Those fixes do not touch timing. `make test` was rerun after them and again on 2026-10-13, and passes.

## 8. How to reproduce

```
make all                 # build, no warnings
make test                # unit, socket and script tests, about 25 s
make test-default-timers # real 20/5/10/60 s timers, about 2 min
bash scripts/demo.sh     # scripted run that prints the server log lines
```

## 9. Conclusion

On both machines (cloud VM and Robert's computer), `make test` ran 110 unit tests, 51 socket tests and all scripted scenarios with no failures, and `make test-default-timers` passed all its checks. TermTrivia meets the Phase 2 requirements. One client can create a game, play every round on the real timers, view the leaderboard, quit or end the game, be killed and reconnect within 60 seconds, and be refused with the specified codes for every invalid case we tried. Malformed and split input never crashed the server, and shutdown with Ctrl+C is clean. The server log is timestamped and never contains a token.

**Remaining risks:**
1. Several clients at once is untested at the socket level. The game logic is tested for it, but T36, T37 and T39 are still to be run in Phase 3.
2. Tests ran on localhost only. Real network delay was not tested.
3. Both runs were on Linux with g++ (13.3 and 16.2). Other platforms were not tried.

**Fix first in Phase 3:** run the pending multi-client tests, then decide what happens to a game whose host never returns.
