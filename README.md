# TermTrivia (CIS 427, Phase 3)

## Project name and purpose

TermTrivia is a terminal multiplayer trivia game over TCP, written in C++17. One server
runs many games at once and each player runs a client in a terminal. A host creates a game
and gets a join code, other players join with the code, the host starts it, and questions
arrive on a timer: 20 seconds per question, 5 seconds between questions, 10 seconds after
the last one. **Scoring is on correctness only:** a correct answer is worth one point no
matter how fast it was typed. A player who loses their connection can reconnect with a
private token and keep their score.

**This is Phase 3: many clients and many games at once.** The server runs one thread per
client and one timer thread per game, and each game has its own lock so a slow client in one
game cannot delay another game (`docs/architecture.md`).

## Team members

Robert and Matthew (CIS 427).

## Required software

- Linux, `g++` with C++17 support, GNU `make`, and `bash` (for the test scripts).
- For `make test`'s deliverable check: `zip`, `unzip`, and `pdfinfo` (poppler-utils).
- No other libraries, no package manager. The tests use our own `tests/minitest.h`.
- Optional, only to rebuild the report PDF: LibreOffice (`soffice`) and `rsvg-convert`.

## Build instructions

```sh
make          # builds ./server and ./client with -Wall -Wextra and no warnings
make clean    # removes the binaries and the build/ folder
```

## Server startup

```sh
./server <port> [--questions FILE] [--log FILE] [--seed N] \
                [--question-secs N] [--intermission-secs N] \
                [--final-intermission-secs N] [--reconnect-secs N]
```

Example: `./server 5000`. Press Ctrl+C to stop it; the server logs `server shutting down`.

## Client startup

```sh
./client <host> <port>
```

Example: `./client localhost 5000`. Start one client per player, each in its own terminal.

## Command-line arguments

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
They must be positive numbers. The client takes the server host and port only.

### Question bank

`questions.txt` holds one question per line, six fields separated by `|`:

```
question text|option A|option B|option C|option D|correct letter
```

Blank lines and lines starting with `#` are ignored. A malformed line is skipped with a
warning in the log. The server refuses to start if no valid question loads.

### Commands

Type these at the client prompt (command names are not case-sensitive; `help` lists them):

| Command | Who | What it does |
|---|---|---|
| `CREATE_GAME <name> <count>` | anyone | create a game with `<count>` questions; prints the join code |
| `JOIN_GAME <code> <name>` | anyone | join a game, in the lobby or after it started |
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
`RECONNECT`. If the host does not return to a lobby within the reconnect window, the lobby
ends with `GAME_END HOST_EXPIRED`.

## Example interaction

Host terminal:

```
$ ./client localhost 5000
> create_game Robert 3
Game created. Join code: 7QXA
Share this code with the other players.
```

Player terminal (a second client, at the same time):

```
$ ./client localhost 5000
> join_game 7QXA Matthew
2 players in game 7QXA
```

Host terminal, then both players:

```
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
Matthew:1
```

The question and the round result appeared by themselves: the client shows pushes from the
server as soon as they arrive, even while you are idle at the prompt.

## Test instructions

```sh
make test                   # unit tests, real-socket tests, and shell scenarios (about 30 s once built)
make test-default-timers    # slow check with the real 20/5/10/60 s timers (about 2.5 minutes)
build/unit_tests T42        # run only tests whose name contains "T42"
build/integration_tests T40
bash tests/scripts/scenarios.sh   # the failure scenarios against the real binaries
bash scripts/measure_nfr.sh       # 20-player timing check with the real timers (about 35 s)
bash scripts/stress_test.sh        # load test: 55 clients in 5 games, 11 000 requests (about 10 s)
bash tests/scripts/restart.sh     # restart behavior: stop the server, restart it, check old sessions (part of make test)
```

`make test` builds the server, client, and test programs first. Test names include the test
ID from `docs/test-plan.md` where there is one. Results are in `docs/phase3-test-report.md`
(Phase 3) and `docs/phase2-test-report.md`.

### Live demo

`bash scripts/demo_phase3.sh` runs the Phase 3 demo end to end: server start, five
simultaneous clients, a duplicate-name race, simultaneous answers, a killed client that
reconnects, an invalid request, graceful termination, and the server log. The talking points
are in `docs/phase3-demo.md`. `bash scripts/demo.sh` is the earlier single-client demo, and
`docs/sample_server.log` is a real log from `bash scripts/make_sample_log.sh`.

### Packaging the draft submission

`bash scripts/package-phase3.sh` writes `dist/termtrivia-phase3-draft.zip` (add
`--verify-build` to extract it and run `make all` and `make test` there). The zip is a draft:
the contribution summary and Ripple reflection in `docs/phase3-technical-report.pdf` are written and approved
by both team members.

## Known limitations

- Each client uses a thread, and each game uses a timer thread; this is simple and fine for the
  tested 20 players and a handful of games, but it is not designed for thousands of connections.
- A client that stops reading is dropped after a 200 ms write timeout; its game's other writes
  wait for that timeout, but other games do not.
- No host transfer: if the host is gone, a started game finishes with no host and a lobby ends.
- Games exist only in memory; stopping the server ends every game.
- No encryption or authentication beyond the private reconnect token; tested on localhost only.
- A terminal is required, and answering under time pressure favors fast typists to the extent
  that a question can time out (scoring itself ignores speed).

More detail and measurements are in `docs/phase3-technical-report.pdf`.

## Attribution for external libraries or resources

No external libraries are used: only the C++ standard library and POSIX sockets. The
question bank in `questions.txt` was written by the team. The test framework is the team's own
`tests/minitest.h`. The project was built with the assistance of AI coding tools (Claude and
Codex) under the team's direction, as recorded in the project history. The report PDF is
rendered with LibreOffice and the diagrams with `rsvg-convert`; neither is part of the
program.

## Repository map

| Path | What is in it |
|---|---|
| `src/common/` | framing and sockets shared by both programs: `line_buffer`, `message_assembler`, `connection`, `protocol`, `net_util` |
| `src/server/` | `game` (rules and timers, no sockets), `question_bank`, `logger`, `config`, `server` (accept loop, handler threads, game timer threads, lanes), `startup`, `server_main` |
| `src/client/` | `client` (two threads), `client_ui`, `reply_queue`, `session_file`, `client_config`, `client_main` |
| `tests/` | `minitest.h`, unit tests, real-socket tests, `helpers/raw_client`, and `scripts/` |
| `docs/` | protocol, requirements, test plan and results, architecture, design changes and summary, walkthrough, Phase 3 technical report, demo guide |
| `scripts/` | demos, sample log, 20-player measurement, packaging |

Start with `docs/walkthrough.md` for a plain-language tour of every part of the code.
