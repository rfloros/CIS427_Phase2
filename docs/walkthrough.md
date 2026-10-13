# Code walkthrough (for the demo)

Written for two students who must explain this code live. After each slice, the agent adds an entry here in plain language. Aim for something a classmate could follow.

Template for each entry:

## Slice N: name

- **What it does:** two or three sentences.
- **Key files and functions:** names, and one line on each.
- **Why it is designed this way:** the reason, not the mechanics.
- **How it connects:** what calls it and what it calls.
- **Likely demo questions:** three or four questions an instructor might ask, with short answers.
- **Try it yourself:** the exact command to run the relevant tests or a quick manual check.

(Entries are added below as slices are completed.)

## Slice 0: Skeleton

- **What it does:** Sets up the folders, the `Makefile`, a tiny test runner, and a
  starter `questions.txt` with 26 questions. The two programs are empty stubs for
  now, so the build and test commands can be trusted before any real code exists.
- **Key files and functions:**
  - `Makefile`: builds `server` and `client` (`make all`) and runs every check (`make test`).
  - `tests/minitest.h`: `TEST_CASE` registers a test and `CHECK` records a failure without stopping.
  - `tests/test_main.cpp`: the `main()` shared by the unit and integration test programs.
  - `tests/scripts/check_questions.sh`: counts valid lines in `questions.txt`.
- **Why it is designed this way:** The task forbids new dependencies, so we use our
  own 50-line test header instead of doctest (logged in `design-changes.md`). `make test`
  depends on the server, client, and test programs, so it cannot run stale binaries.
- **How it connects:** Later slices drop `.cpp` files into `src/common`, `src/server`,
  `src/client`, and `tests/unit`; the Makefile finds them with `wildcard`, so no
  Makefile edits are needed. Everything except the two `*_main.cpp` files is shared
  with the tests.
- **Likely demo questions:**
  - How do I run only one test? `build/unit_tests "T31"` runs tests whose name contains `T31`.
  - Why does `make test` rebuild first? Its prerequisites are the programs and test binaries.
  - Where do the warnings flags live? `CXXFLAGS` in the `Makefile` (`-Wall -Wextra`).
- **Try it yourself:** `make test`

## Slice 1: Framing and connection layer

- **What it does:** Turns the raw bytes that arrive from a TCP socket into
  complete lines, and groups server lines into whole messages. It also sends
  messages safely, one whole message at a time.
- **Key files and functions:**
  - `src/common/line_buffer.*` `LineBuffer`: `append()` adds bytes, `next()` returns the next complete line or an over-length report.
  - `src/common/message_assembler.*` `MessageAssembler`: `add_line()` collects lines until a blank line, then returns a `Message` flagged as push or reply.
  - `src/common/connection.*` `Connection`: owns the socket; `read_line()` and `send_message()`.
- **Why it is designed this way:** TCP is a byte stream, so one `recv()` can return half a
  line or three lines. We keep every byte in a buffer and only act on text ending in
  a newline. Server messages end with a blank line so the client knows where one
  message stops, and the write lock plus one-buffer `send` stops two threads from
  mixing their lines together. `MSG_NOSIGNAL` turns a vanished peer into a
  failed send instead of a crash.
- **How it connects:** The server handler and the client reader thread both call
  `Connection::read_line()`. `LineBuffer` and `MessageAssembler` have no socket calls, so
  tests feed them plain strings.
- **Likely demo questions:**
  - Why can a command arrive in pieces? TCP does not keep message boundaries.
  - What happens to a 600-character line? One `TooLong` event, the rest is thrown away up to the newline, and the next command works.
  - Why a write lock? The timer thread and the handler can both send to the same player.
  - How do we know a peer closed? `recv` returns 0.
- **Try it yourself:** `build/unit_tests T3` (T31, T32, T33 tests) or `make test`.

## Slice 2: Protocol parser and validator

- **What it does:** Reads one line from a client and decides which of the eight
  commands it is (or which error code it earns). It also builds the exact text
  of every reply and push so nothing else has to format protocol text.
- **Key files and functions:**
  - `src/server/protocol.*` `parse_command()`: one line in, a `Command` or an error code (300 or 423) out.
  - `status_line()`: the exact text for each response code.
  - `reply_*()` and `push_*()` builders: return a list of lines (the `Connection` adds the blank line).
  - `sort_standings()`: highest score first.
- **Why it is designed this way:** Putting all wire text in one file with no sockets and
  no game state makes it easy to test byte-for-byte and easy to explain. Names may not
  contain `:` or spaces, so `name:score` lines stay unambiguous.
- **How it connects:** The server handler calls `parse_command()`; game logic calls the builders;
  the client reuses `parse_command()` to check input before sending.
- **Likely demo questions:**
  - What does `CREATE_GAME Robert` return? 423, because the count is missing.
  - What if both name and count are bad? 300, the name is checked first.
  - Are commands case-sensitive? Yes on the wire; the client uppercases the command word for you.
  - Which codes exist? 200, 300, 401, 402, 404, 409, 410, 420, 421, 422, 423 (see `status_line`).
- **Try it yourself:** `build/unit_tests Slice2`

## Slice 3: Game logic

- **What it does:** Holds all game rules: creating and joining games, the question
  timeline, scoring, the leaderboard, ending a game, and disconnect/reconnect.
  It never touches a socket. Instead each call returns a list of "effects" (which
  connection gets which message) and the server layer delivers them.
- **Key files and functions:**
  - `src/server/game.*` `GameManager`: `handle()` runs one command, `on_disconnect()` handles a dropped connection, `tick(now)` advances timers.
  - `src/server/question_bank.*`: reads `questions.txt`, skips bad lines with a warning.
  - `src/server/random_seed.*`: OS entropy for the production random seed.
- **State machine:**

  | State | Becomes | When |
  |---|---|---|
  | LOBBY | QUESTION_ACTIVE | host sends START_ROUND |
  | QUESTION_ACTIVE | INTERMISSION | timer expires, or every ACTIVE player has answered |
  | INTERMISSION | QUESTION_ACTIVE | 5 s pass and questions remain |
  | INTERMISSION | ended (removed) | final 10 s pass: GAME_END COMPLETE |
  | any | ended (removed) | END_GAME or host QUIT |

- **Why it is designed this way:** Time is passed in as a number and randomness comes
  from a seed, so tests need no sleeping and are repeatable. Effects keep networking
  out of the rules (NFR-08) and make the reply-before-push order visible: the reply is
  always the first effect in the list. One mutex guards all state for Phase 3.
- **How reconnect works:** An unexpected drop keeps the player in the game as
  DISCONNECTED_PENDING with the time of the drop. RECONNECT must match name, code,
  and token within 60 seconds; every failure gives the same 410. `tick()` removes
  expired players and removes a game with nobody left.
- **Likely demo questions:**
  - Does a faster answer score more? No, one point for correct, see `close_round()`.
  - What if the host disconnects mid-round? The timer keeps running; with nobody active the round waits for the timer.
  - How are questions chosen? Shuffle the bank with the seeded generator, take the first N, so no repeats.
  - Where do tokens go? Only into the create/join reply; events for the log never include them.
- **Try it yourself:** `build/unit_tests T0` and `build/unit_tests Slice3`

## Slice 4: Logging and configuration

- **What it does:** Writes a timestamped log of what the server does, and reads the
  command-line options for the server and the client.
- **Key files and functions:**
  - `src/server/logger.*` `Logger`: one line per event, `2026-10-12T17:20:07 INFO ...`, flushed at once, with a mutex so the timer thread and handler never mix lines.
  - `src/server/config.*` `parse_server_args()` / `parse_client_args()`: turn arguments into settings or an error message.
  - `src/common/net_util.*` `parse_port()`: whole numbers 1 to 65535 only.
- **Why it is designed this way:** The logger's event functions only accept a command
  *name* and a result code, never the raw request line, so a token has no path into the
  log. Timing flags accept decimals so tests can run in milliseconds, but zero,
  negative, NaN, and infinity are refused.
- **How it connects:** `server_main` parses arguments and opens the log; the handler and timer
  thread call the `Logger` event functions.
- **Likely demo questions:**
  - What is never logged? Tokens and raw request text.
  - Why can timers be `0.2`? Tests and demos need short rounds; defaults stay 20/5/10/60.
  - What happens with `./server 70000`? A usage error and a non-zero exit.
- **Try it yourself:** `build/unit_tests Slice4`

## Slice 5: Server runtime

- **What it does:** Runs the real server: it listens on a port, serves one client at a
  time, and keeps a timer thread that closes rounds and opens questions even when
  nobody types anything.
- **Key files and functions:**
  - `src/server/server.*` `Server`: `accept_loop()` waits for a client; `handle_connection()` reads lines and runs them through the parser and game logic; `timer_loop()` calls `tick()` every 20 ms; `deliver()` writes the effects.
  - `src/server/startup.*` `run_server()`: opens the log, loads questions, picks the seed, starts the server, runs until Ctrl+C.
  - `src/server/server_main.cpp`: parses arguments and installs the Ctrl+C handler.
- **Threads:** the main thread accepts and handles the one client; the timer thread
  advances games. They share the game state through a `coordinator_` mutex.
- **How reply-before-push is guaranteed:** a command holds the coordinator lock while the
  game state changes *and* while its reply and pushes are written, in the order the game
  logic listed them. The timer thread needs the same lock, so it can never slip a push in
  between. (See `docs/architecture.md`.)
- **What happens on disconnect:** `recv` returns 0 (or an error), the handler tells the game
  logic (`on_disconnect`), which keeps the player for 60 seconds, removes the connection from
  the live map, and only then closes the socket.
- **Why it is designed this way:** One simple lock is easier to explain than a writer thread and
  queue, and one client at a time means contention is not a problem. Phase 3 can run the same
  handler once per client.
- **Likely demo questions:**
  - Can the timer and a command write at the same time? No, the coordinator mutex serializes them.
  - What if a client stops reading? Writes time out after 200 ms and the connection is dropped.
  - What if the host's connection drops during a round? The timer keeps running and the host can RECONNECT.
  - How does Ctrl+C stop everything? A signal handler writes to a pipe; the accept loop and handler notice, the timer thread is joined.
- **Try it yourself:** `build/integration_tests T35` or `./server 5000` then `nc localhost 5000` and type `CREATE_GAME Robert 3`.

## Slice 6: Client

- **What it does:** The terminal program a player runs. It lists the commands, checks what you
  type, sends it, and shows replies. Questions and results from the server appear on their
  own even while you are idle at the prompt.
- **Key files and functions:**
  - `src/client/client.*` `Client`: `run()` is the main thread (keyboard); `reader_loop()` is the reader thread (socket).
  - `src/client/client_ui.*`: `interpret_input()` turns a typed line into "send this" or "show this message"; `command_list_text()`, `prompt_text()`.
  - `src/client/reply_queue.*` `ReplyQueue`: hands replies from the reader thread to the main thread.
  - `src/client/session_file.*`: saves name/code/token to `.termtrivia_session` (permissions 0600).
  - `src/client/client_main.cpp`: argument check, `getaddrinfo`/`connect`, clear error if the server is down.
- **The two threads:** The reader thread is the only caller of `recv`. It cuts bytes into messages
  with the Slice 1 classes. A message whose first line starts with `PUSH` is printed immediately;
  anything else is a reply and goes on the queue. The main thread sends one command and waits for
  the next reply from the queue, so a push can never be mistaken for a reply.
- **Why the main thread polls:** It waits on both the keyboard and a small "wake-up" pipe. When the
  server closes or the game ends, the reader writes to the pipe, so an idle client exits without
  needing another key press.
- **The session file:** CREATE_GAME and JOIN_GAME replies contain the private token. The client
  saves it for `RECONNECT` and never prints it. QUIT and GAME_END delete the file.
- **Likely demo questions:**
  - Why two threads? Pushes can arrive at any time, so something must always be listening.
  - Where is the token? Only in `.termtrivia_session`, readable only by you.
  - What if you press Ctrl+D? The client closes without QUIT, so you can `RECONNECT` later.
  - What does `help` send to the server? Nothing; it is handled locally.
- **Try it yourself:** `build/integration_tests T34` and `build/integration_tests Slice6`

## Slice 7: Scripted failure scenarios and results

- **What it does:** Runs the real `server` and `client` programs through the failure cases so they
  can be repeated live: bad input, split and combined sends, a missing server, bad ports, a killed
  client that reconnects, a server that disappears, and Ctrl+C.
- **Key files and functions:**
  - `tests/helpers/raw_client.cpp`: a tiny scripted socket client (`send`, `bytewise`, `sleep`, `read`, `kill`) that shows the exact bytes the server returns.
  - `tests/scripts/scenarios.sh`: the failure scenarios (T14 to T16, T21, T24 to T27, T29, T31 to T33, SIGINT, no tokens in the log). Runs in `make test`.
  - `tests/scripts/default_timers.sh`: the slow check with the real 20/5/10/60 second timers (`make test-default-timers`).
  - `scripts/make_sample_log.sh`: records `docs/sample_server.log` from a real run.
  - `tests/scripts/lib.sh`: starts a server on a free port and cleans up.
- **Why it is designed this way:** Unit tests prove the parts; these scripts prove the programs a
  grader will actually run. The raw client lets us show framing cases (split command, two commands
  in one send) that a normal typing client cannot produce.
- **Likely demo questions:**
  - How do you show a command split across two sends? `build/raw_client 127.0.0.1 PORT send 'CREATE_GAME Rob' sleep 500 send 'ert 3\n' read 300`.
  - Why two kinds of timer test? `make test` uses fast timers; `make test-default-timers` measures the real ones.
  - How do you know tokens are not logged? The scripts search the log for the real token and fail if found.
- **Try it yourself:** `bash tests/scripts/scenarios.sh`
