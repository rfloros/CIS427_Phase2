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
| 34 | 2026-10-12 | If the client's standard input reaches end-of-file, it closes the connection without sending QUIT, exits with status 0, and keeps the saved session file. The server treats this as an unexpected disconnect, so the player can RECONNECT. | Human chose this at Gate 1 (protocol 6.4 treats a missing QUIT as an unexpected disconnect). | FR-21 |
| 35 | 2026-10-12 | The client handles `help` itself and uppercases the command word (arguments keep their case) before sending; a bare `RECONNECT` expands the saved session into a full command. Neither `help` nor a bare RECONNECT reaches the server. | Makes the terminal friendlier while the wire protocol stays case-sensitive. | FR-20, T12 |
| 36 | 2026-10-12 | A GAME_END push makes the client print the final results, delete its session file, and exit with status 0. A server close while the client waits for input or a reply prints a message and exits with status 0. | Required by T07 and T29; the session file is only for resuming a live game. | FR-09, FR-20, T29 |
| 37 | 2026-10-12 | `protocol.*` lives in `src/common/` instead of `src/server/`, because the client reuses it to check input shape before sending. | Avoids duplicating the command rules. NFR-08 separation is unchanged. | NFR-08 |
| 38 | 2026-10-29 | Phase 3 serves many clients at once: one handler thread per connection and one timer thread per game. The global coordinator is replaced by one lock ("lane") per game. A lane is held from a state change until all of its messages are written, so a reply still precedes its pushes. A short routing lock guards the game-code-to-lane map and the live-connection map and never covers socket I/O. Lock order: lane, then routing lock, then the game manager's own lock. Replaces decision 18 and refines decision 31. | A client that stops reading can block writes for the 200 ms send timeout. With one global lock, three such clients in one game delayed another game's reply past 500 ms (T40 failed on that design). Human chose per-game coordination after Critic round 1. | FR-24, NFR-01, NFR-07, T35 to T40, T44 |
| 39 | 2026-10-29 | CREATE_GAME holds the new game's lane from before the game is published until its 200 OK is written, and the game timer thread is started before that reply. If the timer thread cannot start, the game is rolled back and the connection is closed with no reply. This is the one exception to the one-reply rule. | A game that cannot advance must never exist, and a JOIN_GAME must not enter a game that is about to be rolled back. | T44 |
| 40 | 2026-10-29 | Finished handler and timer threads are marked done as their last step; the accept loop joins them after each wake-up, and shutdown joins all of them (handlers first). If a handler thread cannot be created the connection is closed, an error is logged, and the server keeps accepting. | Keeps thread count bounded after many connections and games, and a resource failure must not stop the server. | T44 |
| 41 | 2026-10-29 | New game-end reason `HOST_EXPIRED`. If the host's reconnect window expires while the game is in the LOBBY, the game ends: connected players receive `PUSH GAME_END HOST_EXPIRED` with the standings of the players still in the game (the expired host is already removed) and are disconnected, and the game and every pending reconnect slot are removed, even if nobody is connected. Supersedes the "exactly three endings" claim in protocol 6.4. | Human decided at Gate 1: "End the abandoned lobby and disconnect its players" and "PUSH GAME_END HOST_EXPIRED, then close". Without it, players in a lobby nobody can start would wait forever. | FR-26, T42 |
| 42 | 2026-10-29 | If the host's window expires after START_ROUND, the game continues to its normal COMPLETE ending. No one becomes host, and the expired host is removed from later standings like any expired player. | Human decided at Gate 1: "Keep the game running to completion; no one gets host commands". | FR-22, FR-26, T47 |
| 43 | 2026-10-29 | JOIN_GAME during the final intermission is accepted. The player gets no question, shows score 0 in the final standings, receives `GAME_END COMPLETE`, and is disconnected. | Human decided at Gate 1: "Accept the join; include them in final results". The Phase 2 logic already behaved this way; Phase 3 documents it and tests it. | FR-02, T43 |
