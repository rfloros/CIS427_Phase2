# Implementation plan

Status checklist (tick when the slice's tests pass and its walkthrough entry exists):

- [x] Slice 0: Skeleton
- [x] Slice 1: Framing and connection layer
- [x] Slice 2: Protocol parser and validator
- [x] Slice 3: Game logic
- [x] Slice 4: Logging and configuration
- [x] Slice 5: Server runtime
- [x] Slice 6: Client
- [x] Slice 7: Scripted failure scenarios and results
- [ ] Slice 8: Deliverables

Test IDs refer to docs/test-plan.md. "Phase 3" tests are written as unit tests where possible but not run over real simultaneous sockets until Phase 3.

## Slice 0: Skeleton

**Do:** create the folders from START_HERE.md, a Makefile (targets all, server, client, test, clean) with -std=c++17 -Wall -Wextra and -pthread, a vendored test header, one trivial passing test, and questions.txt with at least 20 sample questions.

**Done when:** make builds (empty main functions are fine) and make test runs and passes.

**Walkthrough note:** how the Makefile is organized and how to run one test.

## Slice 1: Framing and connection layer

**Covers:** FR-24, NFR-04, part of FR-18 and NFR-09. Shared by client and server.

**Tests first (red):** T31, T32, T33, T21 at unit level, plus:
- A LineBuffer fed the bytes of one line in one chunk, in two chunks, and one byte at a time returns the same single line.
- Two or more lines in one chunk come out as separate lines in order. A partial line stays in the buffer until completed.
- A trailing carriage return before the newline is stripped.
- A line longer than 512 bytes (including the newline) is reported as over-length, and input is discarded up to the next newline, after which normal lines work again.
- A MessageAssembler turns lines into messages: it collects lines until an empty line, then returns the whole message. It reports whether the first line starts with PUSH (push) or not (reply). Two messages in one chunk come out as two messages.
- A closed peer (recv returns 0) is reported as closed. Use socketpair() for socket-level tests.
- send_all handles partial sends (loop until everything is written) and does not crash on a closed peer (use MSG_NOSIGNAL).
- Two threads sending messages on one Connection at the same time never interleave (write lock). Messages are always sent whole.

**Implement:**
- LineBuffer: append bytes, extract complete lines, keep the partial remainder, enforce the 512-byte limit.
- MessageAssembler: lines to messages, with the push/reply classification.
- Connection: owns the fd, a receive LineBuffer, and a write mutex. read_line() blocks until a line, an over-length event, or close. send_message(lines) writes all lines plus the terminating blank line as one buffer while holding the write lock.
- These classes must be testable without real sockets (feed strings in, read lines out).

**Done when:** all the tests above pass, and the pure parts (LineBuffer, MessageAssembler) have no socket calls.

**Walkthrough note:** why TCP is a byte stream, what the receive buffer does, why messages end with a blank line, and why writes need a lock.

## Slice 2: Protocol parser and validator

**Covers:** FR-14, FR-18, part of FR-19. No sockets, no game state.

**Tests first (red):** T14, T15, T16, T20, T21 at parse level, plus:
- Every command in protocol.md section 4 parses to a typed Command with its fields.
- Rejections: empty line, unknown command, wrong argument count, name that is empty, over 20 characters, contains a space (cannot occur after splitting, so test extra arguments), or non-ASCII; join code not 4 alphanumeric characters; answer choice not A, B, C, or D (case-insensitive accepted); num_questions not an integer or outside 1 to the number available (the available count is passed in).
- The over-length case maps to 300.
- Exact reply text for every code in protocol.md section 3 (200, 300, 401, 402, 404, 409, 410, 420, 421, 422, 423), including the number substituted into 423.
- Builders produce exact message text for replies and pushes: CREATE_GAME and JOIN_GAME replies, LEADERBOARD, RECONNECT, QUESTION, ROUND_RESULT, GAME_END.

**Implement:** parse_command(line, available_questions) returning either a Command or an error code, and message builders returning vectors of lines (the Connection adds the blank line). Names and codes are case-sensitive for names, case-insensitive for join codes and answer choices (protocol 6.4).

**Done when:** all tests pass and the module includes no socket or game-state headers.

**Walkthrough note:** the command list, how a line becomes a Command, and the table of error codes.

## Slice 3: Game logic

**Covers:** FR-01 to FR-12, FR-13 to FR-19, FR-21 to FR-23 (logic level). No sockets. Time comes from a clock passed in (a fake clock in tests), and randomness from an injectable generator.

**Tests first (red):** T01, T03, T05, T06, T07, T08, T09, T18, T19, T17, T27, T28, T26, T30 at logic level, and the Phase 3 logic cases T10, T11, T22, T23, T38 as unit tests. Plus:
- Game code and token are 4 characters, alphanumeric, and the code is unique among active games.
- Questions for a game are chosen at random without repeats, from the loaded bank.
- State machine: LOBBY, QUESTION_ACTIVE, INTERMISSION, ENDED. START_ROUND only from LOBBY (otherwise 421). SUBMIT_ANSWER only in QUESTION_ACTIVE (otherwise 420) and once per round (otherwise 422).
- A round closes when the timer expires, or early when there is at least one ACTIVE player and every ACTIVE player has answered. With zero ACTIVE players it waits for the timer (see design-changes.md).
- Scoring: a correct answer adds exactly 1 point regardless of when it was submitted.
- Joining during QUESTION_ACTIVE or INTERMISSION makes the player WAITING. WAITING players become ACTIVE when the round result is produced.
- Intermission is 5 seconds, or 10 seconds after the final question. After the final intermission the game ends with COMPLETE.
- END_GAME and host QUIT end the game with HOST_ENDED and HOST_QUIT. Non-host QUIT removes only that player. A non-host sending a host command gets 402.
- Unexpected disconnect: the player becomes DISCONNECTED_PENDING for the reconnect window. RECONNECT with the right name, code, and token inside the window restores the player. Any mismatch, unknown game, ended game, or expired window gives the same 410. A reconnect during QUESTION_ACTIVE also yields a repeated QUESTION effect.
- If every player is DISCONNECTED_PENDING and all windows expire, the game is removed.
- LEADERBOARD lists scores highest first.

**Implement:**
- QuestionBank loader (file format in START_HERE.md).
- A Game and a GameManager (Active Games Table plus Disconnected Sessions Table). Every public operation takes the current time and returns a result plus a list of effects: (player or connection id, message lines). A tick(now) function advances timers. Everything is guarded by a mutex.
- Do not include any socket header here.

**Done when:** all the tests pass using only the fake clock (no sleeping).

**Walkthrough note:** the state machine drawn as a small table, how effects keep game logic separate from sockets, and how reconnect works.

## Slice 4: Logging and configuration

**Covers:** FR-25, NFR-09, NFR-11.

**Tests first (red):** T13 at unit level, plus:
- Each log line starts with an ISO-8601 timestamp (for example 2026-10-12T17:20:07) and a level (INFO, WARN, ERROR).
- Required events exist as functions: server startup, listening address and port, client connected (with client id and address), request type and outcome, protocol error, client disconnected, server shutdown.
- A guard test: logging a RECONNECT request or a reply that contains a token never writes the token. Log only the command name and outcome, not the raw line.
- Command-line parsing: valid port, invalid port (abc, 0, 70000), unknown flag, and each timing flag including decimals.

**Implement:** Logger (thread-safe, writes to a file and flushes each line), config parsing for the server and client command lines.

**Done when:** tests pass.

**Walkthrough note:** log format, what is deliberately not logged, and the command-line options.

## Slice 5: Server runtime

**Covers:** FR-03, FR-07, FR-09, FR-21, FR-22, FR-24, NFR-01, and the socket-level versions of earlier tests.

**Tests first (red):** integration tests that start the real server on an ephemeral port with fast timers (for example question 0.5 s, intermission 0.2 s, final 0.3 s, reconnect 1 s) and talk to it over real sockets: T02, T03, T04, T07, T08, T09, T35, T13, plus the socket-level versions of T14, T15, T18, T19, T31, T32, T33, T21, T26, T27, T28, T30. T35 (reply before first push) repeats the START_ROUND exchange 50 times.

**Implement:**
- Accept loop (one client at a time in Phase 2), a handler function per connection (read line, parse, call game logic, write reply, then write any pushes), and a Game Timer Thread that calls tick(now) frequently (about every 50 ms) and delivers effects.
- Always write the reply before any push triggered by the same command (protocol 6.3).
- On client disconnect (recv returns 0 or an error that is not QUIT), tell game logic the player disconnected.
- On GAME_END the server closes the sockets of that game's players and keeps listening.
- Graceful shutdown on SIGINT: log shutdown, close sockets, stop the timer thread, join threads.
- The server never crashes on bad input or a client vanishing (NFR-02).

**Done when:** all integration tests pass, repeated runs are stable (run the suite 10 times), and a manual run with default timers behaves like the protocol.

**Walkthrough note:** thread diagram (accept loop, handler, timer), how the reply-before-push order is guaranteed, and what happens on disconnect.

## Slice 6: Client

**Covers:** FR-04, FR-20, NFR-05, NFR-06, FR-24 (client side).

**Tests first (red):** T12, T24, T25, T29, T34. T34 uses a small fake server (a test helper) that sends awkward byte chunks: one byte at a time, a reply and a push in one chunk, and a push arriving between a command and its reply.

**Implement:**
- Command line ./client host port with port validation (1 to 65535) and a clear message when the connection fails.
- A command interface: on connect print the list of commands and their formats. The user types commands such as CREATE_GAME Robert 3 (case-insensitive command names accepted), plus help. Validate format locally and show the valid format on a mistake, then re-prompt (no exit).
- Reader thread: the only caller of recv. It assembles messages with the Slice 1 classes. A message whose first line starts with PUSH is printed at once and the prompt is shown again. Any other message goes on a thread-safe reply queue. The main thread sends a command and waits for the next reply from the queue.
- If the server closes the connection, print a clear message and exit cleanly.
- Token handling: the client remembers the token from CREATE_GAME and JOIN_GAME replies and never displays it. It also saves name, code, and token to a small session file (.termtrivia_session in the current directory, permissions 0600) so a restarted client can reconnect after a crash. A reconnect command with no arguments uses that file. The file is deleted on QUIT and on GAME_END. Log this decision in design-changes.md.
- QUIT sends QUIT, waits for the reply, closes, and exits with status 0.

**Done when:** tests pass, and a manual run against the real server plays a full solo game.

**Walkthrough note:** the two threads and the reply queue, how a push is told apart from a reply, and the session file.

## Slice 7: Scripted failure scenarios and results

**Covers:** the remaining Phase 2 tests and the assignment's error-scenario demo.

**Do:**
- Script (shell or Python) tests that drive the real binaries for T24, T25, T26, T29, T31, T32, T33, T21 so they can be rerun at the demo.
- Run the entire Phase 2 test list and record the Actual result and Pass/Fail for each in docs/test-plan.md. Phase 3 tests stay "Not run yet".
- Produce a server log sample from a real run that includes startup, a connection, several requests, an error, and a disconnect: save it as docs/sample_server.log.

**Done when:** every Phase 2 test has a recorded result, and the log sample exists.

## Slice 8: Deliverables

**Do:**
- README.md in the repo root: what the project is, requirements, how to build (make), how to run the server and client (with the command lines in START_HERE.md), the command list, how to run tests, and a short example session.
- Sync docs/protocol.md and docs/architecture.md with any approved deviations.
- Write a one-page design-change summary from docs/design-changes.md.
- Finish docs/walkthrough.md.

**Done when:** a teammate can build and run everything from the README alone.

## Slice to test map

| Slice | Tests |
|---|---|
| 1 Framing | T31, T32, T33, T21 (unit level) |
| 2 Protocol | T14, T15, T16, T20, T21 (parse level) |
| 3 Game logic | T01, T03, T05, T06, T07, T08, T09, T18, T19, T17, T27, T28, T26, T30 (logic level); Phase 3 logic cases T10, T11, T22, T23, T38 |
| 4 Logging | T13 (unit level) |
| 5 Server runtime | T02, T03, T04, T07, T08, T09, T35, T13, T14, T15, T18, T19, T31, T32, T33, T21, T26, T27, T28, T30 (over sockets) |
| 6 Client | T12, T24, T25, T29, T34 |
| 7 Scenarios | T24, T25, T26, T29, T31, T32, T33, T21 scripted |
| Phase 3 only | T36, T37, T39, plus socket-level T10, T11, T22, T23, T38 |

## Demo script (what the live demo should show)

1. Start the server with defaults and show the first log lines (startup, listening address and port).
2. Start the client and connect. Show the command list.
3. CREATE_GAME with a small number of questions, then START_ROUND. Show the question arriving on its own.
4. SUBMIT_ANSWER, show the round result push, then LEADERBOARD.
5. Send two invalid requests: an unknown command and CREATE_GAME with a bad count. Show the error replies and that the client keeps working.
6. Kill the client and reconnect with the saved session. Show the score is kept.
7. Show a framing case with the scripted raw-socket test: a command split across two sends, and two commands in one send.
8. END_GAME or QUIT, then show the matching server log lines.

Each team member explains at least one component. Use docs/walkthrough.md for the explanations.
