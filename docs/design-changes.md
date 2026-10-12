# Design changes and decisions

Log every deviation from the Phase 1 design or the spec here, with a reason. This file becomes the brief design-change summary for the Phase 2 submission.

## Part 1: Changes already made (Phase 1 to protocol revision 2)

| # | Change | Why |
|---|---|---|
| 1 | Every server message now ends with a blank line, stated as an explicit rule (protocol 2.2). CREATE_GAME, JOIN_GAME, and RECONNECT replies now terminate too. | Professor feedback: the terminator was only implied by examples. |
| 2 | All pushes start with PUSH and replies start with a 3-digit code. ROUND_RESULT no longer starts with 200 OK. The old 210 notice is replaced by PUSH GAME_END with a reason. | Professor feedback: the client could not tell pushes from replies. |
| 3 | Added explicit receive-buffering, over-length, and atomic-write rules (protocol 6.1 to 6.3). | Professor feedback: partial reads were not addressed. |
| 4 | END_GAME ends only that game and the server keeps listening. | The old text implied a single-game server, which contradicted the Active Games Table design. |
| 5 | Automatic game end now pushes GAME_END to everyone, including the host. | The old text never notified the host on automatic end. |
| 6 | Host QUIT ends the game. An unexpected host disconnect does not. | Previously undefined. |
| 7 | RECONNECT re-pushes the current question if a round is live. | Reconnecting mid-round otherwise left the player waiting. |
| 8 | Typos and formatting glitches fixed. | Cleanup. |
| 9 | Removed "create a username" and "change a username". Names are given in CREATE_GAME or JOIN_GAME and are unique per game. | The protocol has no username command. |
| 10 | Removed "if the host disconnects, another host is chosen". The host can reconnect, and a host QUIT ends the game. | Host transfer was never defined in the protocol. |
| 11 | Requirements and tests now have IDs (FR, NFR, T) and are cross-referenced. | Professor feedback: requirements and tests were inconsistent. |
| 12 | The performance requirement is now measurable (1 s push, 500 ms reply, plus or minus 1 s timers). | Professor feedback: "quickly" was not testable. |

## Part 2: Decisions made while planning Phase 2 (review these)

| # | Decision | Why |
|---|---|---|
| 13 | Server timing values are command-line options, defaulting to the spec values (20, 5, 10, 60 seconds). | Tests cannot wait 20 seconds per round. The spec values are unchanged as defaults. |
| 14 | Question bank file format: one question per line, fields separated by a vertical bar (see START_HERE.md). | The format was not defined. |
| 15 | A round closes early only when at least one ACTIVE player exists and every ACTIVE player has answered. With zero ACTIVE players it waits for the timer. | Otherwise "everyone answered" is vacuously true and rounds would close instantly when the host is disconnected. |
| 16 | If every player is DISCONNECTED_PENDING and all reconnect windows expire, the game is removed. | Avoids a game that nobody can reach or end. |
| 17 | The client saves name, code, and token to a 0600 session file so a restarted client can reconnect. The file is removed on QUIT and GAME_END. The token is never displayed. | Without it, the "client crashed, reconnect" scenario is impossible with the real client. |
| 18 | The Phase 2 server serves one client at a time. A second client waits in the listen backlog. | Phase 2 scope is one client. |
| 19 | Game logic returns "effects" (messages to send) instead of calling sockets. | Keeps game logic separate from networking (NFR-08) and makes it testable with a fake clock. |

## Part 3: Log new decisions below

Add a row: number, date, what changed, why, which requirement or test it affects.

| # | Date | Decision | Why | Affects |
|---|---|---|---|---|
| 20 | 2026-10-12 | Tests use our own `tests/minitest.h` plus shell scripts instead of vendoring doctest. | The human chose this (Gate 1 answer "Use minitest and shell scripts"); START_HERE.md allows the fallback and forbids new dependencies. | All tests |
