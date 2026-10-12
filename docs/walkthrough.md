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
