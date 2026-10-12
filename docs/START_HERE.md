# START HERE: TermTrivia (CIS 427, Phase 2)

This file tells an agent WHAT to build and in what order. HOW to work (TDD steps, git habits, when to pause) is defined in the repo's AGENTS.md. Follow that for process. This file does not override it.

## The project in one paragraph

TermTrivia is a terminal multiplayer trivia game over TCP, written in C++. One server runs games, and each player runs a client in a terminal. A host creates a game and gets a join code, players join with the code, the host starts the game, and then questions arrive on a timer (20 s per question, 5 s intermission, 10 s after the last question). Scoring depends only on correctness, never speed. Players who lose their connection can reconnect with a private token and keep their score.

**This is Phase 2: one client and one server.** The server accepts one active client at a time. A single host can create a game, start it, answer, view the leaderboard, end it, quit, and reconnect. Multiple simultaneous clients are Phase 3, but design for it now (see "Design for Phase 3" below).

## Two rules that shape everything

1. **The code will be demonstrated live and explained by two students.** AI-written code is allowed, but both team members must be able to explain every part. So: keep code simple and readable, prefer small functions with clear names, comment the why (not the what), avoid clever tricks, and after each slice add a plain-language entry to docs/walkthrough.md.
2. **The spec files are the source of truth.** Do not change behavior silently. If the code must differ from the spec, or the spec is unclear, write the decision in docs/design-changes.md before or with the change.

## Read in this order

1. docs/protocol.md: every command, reply, push, code, and the framing and buffering rules.
2. docs/requirements.md: FR-01 to FR-25 and NFR-01 to NFR-11, each with its test IDs.
3. docs/test-plan.md: tests T01 to T39. Tests marked Phase 3 are not run now.
4. docs/architecture.md: components, threads, and two diagrams (docs/images/).
5. docs/implementation-plan.md: the ordered slices and the status checklist. This is your task list.
6. docs/design-changes.md: decisions already made, and where to log new ones.

## Constraints

- Language: C++17, built with g++ on Linux. POSIX sockets (sys/socket.h), std::thread, std::mutex, std::condition_variable.
- Build: a plain Makefile in the repo root with targets all, server, client, test, clean. No CMake, no package manager.
- Dependencies: none at runtime. For tests use one vendored single-header library (doctest: tests/doctest.h). If it cannot be downloaded, write a tiny tests/minitest.h with TEST_CASE and CHECK macros instead and log that in design-changes.md.
- Warnings: compile with -std=c++17 -Wall -Wextra and keep the build warning-free.
- No hard-coded machine-specific paths or addresses. Port, host, and file paths come from the command line.
- ASCII text only on the wire. Lines are at most 512 bytes including the newline.
- Do not log or print tokens, anywhere (see NFR-09).

### Command lines

    ./server <port> [--questions FILE] [--log FILE] [--seed N]
                    [--question-secs N] [--intermission-secs N]
                    [--final-intermission-secs N] [--reconnect-secs N]
    ./client <host> <port>

Defaults: questions file questions.txt, log file server.log, question 20, intermission 5, final intermission 10, reconnect window 60 (all seconds). The timing flags accept decimals (for example 0.2) so tests run in milliseconds. The spec values are the defaults and must never be changed there. --seed makes random choices (join codes, tokens, question selection) repeatable for tests.

### Question bank file (questions.txt)

One question per line, six fields separated by a vertical bar:

    question text|option A|option B|option C|option D|correct letter

- The correct letter is A, B, C, or D.
- Blank lines and lines starting with # are ignored.
- A malformed line is skipped with a warning in the log. The server refuses to start if fewer than 1 valid question loads.
- No field may contain a vertical bar or be empty. ASCII only.
- Ship at least 20 general-knowledge sample questions.

### Suggested source layout

    Makefile
    questions.txt
    src/common/   line_buffer.*, message_assembler.*, connection.*   (framing and sockets, shared by client and server)
    src/server/   protocol.*, game.*, question_bank.*, game_timer.*, logger.*, server.*, server_main.cpp
    src/client/   client_ui.*, reply_queue.*, client.*, client_main.cpp
    tests/        doctest.h, unit tests, integration tests, scripted raw-socket tests
    docs/         (this folder)

You may adjust names, but keep these separations (NFR-08): framing, protocol parsing, game logic, timer, logging, user interface. Game logic must not call socket functions.

## Design for Phase 3 now

Phase 3 adds simultaneous clients. Do these now so Phase 3 is a small change:
- Keep all game state behind a mutex from the start. The timer thread already runs in Phase 2 and shares state with the handler thread.
- Write the per-client handler as a function that takes one connection, not as code inside main.
- Game logic returns "effects" (which messages go to which player) and never touches sockets. The server layer delivers them.
- The accept loop in Phase 2 serves one client at a time. A second client waits in the listen backlog until the first one ends. Do not spawn a thread per client yet.

## The slices (summary)

Work through these in order. Details, tests, and done criteria are in docs/implementation-plan.md.

| Slice | What | Main requirements |
|---|---|---|
| 0 | Skeleton: Makefile, folders, test runner works | none |
| 1 | Framing and connection layer | FR-24, NFR-04 |
| 2 | Protocol parser and validator (no sockets, no game state) | FR-14, FR-18 |
| 3 | Game logic (no sockets, fake clock) | FR-01 to FR-12, FR-13 to FR-23 |
| 4 | Logging and configuration | FR-25, NFR-09, NFR-11 |
| 5 | Server runtime: accept loop, handler, timer thread | FR-03, FR-07, FR-09, FR-21, FR-22, NFR-01 |
| 6 | Client | FR-04, FR-20, NFR-05, NFR-06 |
| 7 | Scripted failure scenarios, log sample, test results | all Phase 2 tests |
| 8 | Deliverables: README, test report, design-change summary, doc sync | n/a |

## Per-slice loop

For each slice, in order:
1. Read the slice in docs/implementation-plan.md and the requirements and tests it names.
2. Write the failing tests first. Name each test case with its test ID (for example "T31 split read").
3. Implement until the tests pass. Run the whole suite (make test), not just the new tests.
4. Add a short entry to docs/walkthrough.md for the slice.
5. Log any deviation or new decision in docs/design-changes.md.
6. Tick the slice in the status checklist in docs/implementation-plan.md, then move on. Follow AGENTS.md for when to stop and check in.

## Definition of done for Phase 2

- make builds server and client with no warnings. make test passes all Phase 2 tests.
- Every Phase 2 test in docs/test-plan.md has its Actual result and Pass/Fail filled in.
- Deliverables exist: client and server source, Makefile, README with run instructions, updated protocol and architecture docs, a server log sample (tests/sample_server.log or docs/), a Phase 2 test report, a brief design-change summary.
- The demo works: server start, client connect, at least three operations, a server-side data update, a valid exchange, at least two invalid requests, recovery after an error, graceful quit, and the matching log entries. See the demo script at the end of docs/implementation-plan.md.
