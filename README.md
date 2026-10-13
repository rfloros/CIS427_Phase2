# TermTrivia (CIS 427, Phase 2)

TermTrivia is a terminal multiplayer trivia game over TCP, written in C++17. One server
runs the games and each player runs a client in a terminal. A host creates a game and
gets a join code, starts it, and then questions arrive on a timer: 20 seconds per
question, 5 seconds between questions, 10 seconds after the last one. A correct answer is
worth one point no matter how fast it was typed. A player who loses their connection can
reconnect with a private token and keep their score.

**This is Phase 2: one client and one server.** The server serves one client at a time (a
second client waits until the first leaves). A single host can create a game, start it,
answer, view the leaderboard, end it, quit, and reconnect. Several simultaneous clients are
Phase 3; the code is already organized for it (see `docs/architecture.md`).

## Requirements

- Linux, `g++` with C++17 support, GNU `make`, and `bash` (for the test scripts).
- No other libraries, no package manager. The tests use our own `tests/minitest.h`.

## Build

```sh
make          # builds ./server and ./client with -Wall -Wextra and no warnings
make clean    # removes the binaries and the build/ folder
```

## Run

Start the server in one terminal, then the client in another:

```sh
./server <port> [--questions FILE] [--log FILE] [--seed N] \
                [--question-secs N] [--intermission-secs N] \
                [--final-intermission-secs N] [--reconnect-secs N]
./client <host> <port>
```

Example: `./server 5000` and `./client localhost 5000`.

| Option | Default | Meaning |
|---|---|---|
| `<port>` | required | 1 to 65535 |
| `--questions FILE` | `questions.txt` | question bank |
| `--log FILE` | `server.log` | server log (appended to) |
| `--seed N` | OS entropy | makes join codes, tokens, and question order repeatable (for tests) |
| `--question-secs N` | 20 | time a question stays open |
| `--intermission-secs N` | 5 | pause between questions |
| `--final-intermission-secs N` | 10 | pause after the last question |
| `--reconnect-secs N` | 60 | how long a dropped player can reconnect |

The timing options take decimals such as `0.2` so tests and demos can run in milliseconds.
They must be positive numbers. Press Ctrl+C to stop the server.

### Question bank

`questions.txt` holds one question per line, six fields separated by `|`:

```
question text|option A|option B|option C|option D|correct letter
```

Blank lines and lines starting with `#` are ignored. A malformed line is skipped with a
warning in the log. The server refuses to start if no valid question loads.

## Commands

Type these at the client prompt (command names are not case-sensitive; `help` lists them):

| Command | Who | What it does |
|---|---|---|
| `CREATE_GAME <name> <count>` | anyone | create a game with `<count>` questions; prints the join code |
| `JOIN_GAME <code> <name>` | anyone | join a game (Phase 3 for a second player) |
| `START_ROUND` | host | start the game; everything after is automatic |
| `SUBMIT_ANSWER <A\|B\|C\|D>` | player | answer the open question |
| `LEADERBOARD` | player | show the scores, highest first |
| `RECONNECT` | anyone | rejoin after a disconnect, using the saved session |
| `END_GAME` | host | end the game for everyone |
| `QUIT` | anyone | leave for good (a host quitting ends the game) |

Names are 1 to 20 characters with no spaces and no colon. Your private token is never
shown; the client keeps it in `.termtrivia_session` (readable only by you) in the folder
you started the client from, and removes the file on QUIT or at the end of the game.
If you press Ctrl+D the client closes without QUIT, so you can start it again and type
`RECONNECT`.

## Example session

```
$ ./client localhost 5000
Commands (not case-sensitive): ...
> create_game Robert 3
Game created. Join code: 7QXA
> start_round
200 OK

QUESTION 1 What is the capital of France?
A) London
B) Paris
C) Berlin
D) Madrid
> submit_answer b
200 OK

ROUND_RESULT 1
Correct answer: B
Robert:1
> leaderboard
200 OK
Robert:1
```

The question and the round result appeared by themselves: the client shows pushes from the
server as soon as they arrive, even while you are idle at the prompt.

## Tests

```sh
make test                   # unit tests, real-socket tests, and shell scenarios (about 25 s once built)
make test-default-timers    # slow check with the real 20/5/10/60 s timers (about 2.5 minutes)
build/unit_tests T31        # run only tests whose name contains "T31"
build/integration_tests T35
bash tests/scripts/scenarios.sh   # the failure scenarios against the real binaries
```

`make test` builds the server, client, and test programs first. Test names include the test
ID from `docs/test-plan.md` where there is one. Results are recorded in
`docs/test-plan.md` and `docs/phase2-test-report.md`.

## Live demo

`bash scripts/demo.sh` runs the demo from `docs/implementation-plan.md` end to end: server
start, client connect, create/start/answer/leaderboard, two invalid requests and recovery,
a killed client that reconnects with its saved session, framing examples, and the matching
log lines. To show the framing cases by hand:

```sh
build/raw_client 127.0.0.1 5000 send 'CREATE_GAME Rob' sleep 500 send 'ert 3\n' read 300
build/raw_client 127.0.0.1 5000 send 'CREATE_GAME Robert 3\n' send 'LEADERBOARD\nLEADERBOARD\n' read 400
```

`docs/sample_server.log` is a real server log from `bash scripts/make_sample_log.sh`.

## Repository map

| Path | What is in it |
|---|---|
| `src/common/` | framing and sockets shared by both programs: `line_buffer`, `message_assembler`, `connection`, `protocol`, `net_util` |
| `src/server/` | `game` (rules and timers, no sockets), `question_bank`, `logger`, `config`, `server` (accept loop, handler, timer thread), `startup`, `server_main` |
| `src/client/` | `client` (two threads), `client_ui`, `reply_queue`, `session_file`, `client_config`, `client_main` |
| `tests/` | `minitest.h`, unit tests, real-socket tests, `helpers/raw_client`, and `scripts/` |
| `docs/` | protocol, requirements, test plan and results, architecture, design changes and summary, walkthrough |

Start with `docs/walkthrough.md` for a plain-language tour of every part of the code.
