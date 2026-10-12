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
| 21 | 2026-10-12 | CREATE_GAME with a valid name but no count returns 423. Extra arguments or an invalid name return 300, and the name is checked first when both fields are bad. | The protocol did not say what a missing count or extra arguments return (human chose these at Gate 1). | FR-14, T16 |
| 22 | 2026-10-12 | A player name containing a colon is malformed (300). | Leaderboard lines are `name:score`, so a colon would be ambiguous. | FR-18 |
| 23 | 2026-10-12 | CREATE_GAME, JOIN_GAME, or RECONNECT on a connection that is already in a game returns 300 and changes nothing. | Previously undefined. | FR-18 |
| 24 | 2026-10-12 | QUIT from a connection that is not in a game returns 200 OK and then closes the connection. | Previously undefined. | FR-11 |
| 25 | 2026-10-12 | SUBMIT_ANSWER from a WAITING player returns 420. | A waiting player never saw the question, so for them no round is open. | FR-19 |
| 26 | 2026-10-12 | RECONNECT with a malformed code or token (not 4 letters or digits) returns 300. A well-formed request with wrong, expired, or unknown details returns the same generic 410. Tokens are matched case-sensitively. | Keeps the generic-410 secrecy rule for guesses while still reporting format mistakes. | FR-12, T28 |
| 27 | 2026-10-12 | RECONNECT during an INTERMISSION reports the number of the last question that was opened. | The protocol only defined question numbers for LOBBY and live rounds. | FR-12, T27 |
| 28 | 2026-10-12 | Timing command-line overrides must be positive finite numbers. | Zero, negative, or NaN timers would break round logic. | NFR-11 |
| 29 | 2026-10-12 | Host QUIT sends the quitting host only `200 OK` before closing; other connected players get GAME_END HOST_QUIT. | Clarifies protocol 4.7 (the host asked to leave, so it needs no end notice). Human-approved at Gate 1. | FR-11, T09 |
| 30 | 2026-10-12 | ROUND_RESULT standings are sorted highest first, like LEADERBOARD and GAME_END. | The protocol example for ROUND_RESULT was unsorted, but it calls it a leaderboard. | FR-07 |
| 31 | 2026-10-12 | Phase 2 delivery uses one coordinator mutex and direct, bounded socket writes (200 ms send timeout) instead of a dedicated writer thread and outbound queue. See architecture.md. | Simplest design that still guarantees reply-before-push. Human chose it at Gate 1. | FR-24, T35 |
| 32 | 2026-10-12 | Ctrl+C is handled with a wake-up pipe; the server logs `server shutting down` and sends no protocol message. | The protocol defines no push for server shutdown. | T13 |
| 33 | 2026-10-12 | After the server ends a session on purpose it half-closes, waits up to 0.5 s for the client to close, then closes. | Closing a socket that still has unread input resets the connection and can drop the final GAME_END message. | T07, T08 |

