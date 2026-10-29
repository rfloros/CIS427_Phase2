# TermTrivia design-change summary (Phase 2 and Phase 3)

Full list with reasons: `docs/design-changes.md`. The protocol (revision 2) is unchanged
in structure; the changes below fill gaps it left open or are implementation choices.

## Protocol clarifications (human-approved at Gate 1)

- **Missing or extra CREATE_GAME arguments.** A valid name with no count returns 423; extra
  arguments or an invalid name return 300, and the name is checked first (decision 21).
- **Names** may not contain a colon, because leaderboard lines are `name:score` (22).
- **Already joined.** CREATE_GAME, JOIN_GAME, or RECONNECT on a connection already in a game
  returns 300 and changes nothing (23).
- **QUIT outside a game** returns 200 OK and closes the connection (24).
- **WAITING players** (joined mid-round) get 420 for SUBMIT_ANSWER (25).
- **RECONNECT.** Malformed code or token returns 300; any well-formed but wrong, expired, or
  unknown request returns the same generic 410. Tokens are case-sensitive (26). During an
  INTERMISSION it reports the last question number that was opened (27).
- **Host QUIT.** The host receives only `200 OK` before the connection closes; other players
  receive `GAME_END HOST_QUIT` (29).
- **ROUND_RESULT** standings are sorted highest first, like LEADERBOARD (30).

## Phase 3 protocol changes (human-approved at Gate 1)

- **New end reason `HOST_EXPIRED`.** A lobby whose host did not reconnect within the window ends
  for everyone: connected players get `PUSH GAME_END HOST_EXPIRED` and are disconnected, and the
  lobby and every pending reconnect slot are removed (41).
- **Host expiry during play.** The game continues to COMPLETE; nobody gets host powers; the expired
  host leaves the standings (42).
- **Join in the final intermission** is accepted; the player appears with score 0 in the final
  standings (43).

## Implementation decisions

- **Configurable timers.** Command-line options override 20/5/10/60 s so tests run fast; the spec
  values stay the defaults, and overrides must be positive (13, 28).
- **Question bank format.** One question per line, `|`-separated, at least one valid question
  required, malformed lines skipped with a warning (14).
- **Early round close** needs at least one ACTIVE player; with none, the timer decides (15). A game
  whose players have all timed out is removed (16).
- **Delivery.** Direct, bounded writes (200 ms send timeout) instead of a writer thread and queue;
  Phase 2 used one coordinator mutex (31), Phase 3 uses one lock per game (38). Ctrl+C uses a
  wake-up pipe (32). After an intentional close the server waits up to 0.5 s for the client so the
  final message is not lost (33).
- **Client.** Saved 0600 session file for RECONNECT (17); Ctrl+D closes without QUIT and keeps the
  session (34); `help` and bare RECONNECT are local, command words are uppercased (35); GAME_END and
  server close end the client cleanly (36).
- **Testing.** Our own `tests/minitest.h` and shell scripts instead of doctest (20). `protocol.*`
  lives in `src/common/` because the client reuses it (37).
- **Phase 2 scope.** One client at a time (18, replaced in Phase 3 by 38); game logic returns
  effects and never touches sockets (19), so Phase 3 only had to run the existing handler on a
  thread per client.
- **Phase 3 concurrency.** One handler thread per client, one timer thread per game, and one
  ordering lock ("lane") per game replace the single coordinator mutex, so a stalled client in one
  game cannot delay another game (38). A new game's lane is held until its 200 OK is written, and a
  game whose timer thread cannot start is rolled back with no reply (39). Finished threads are
  joined by the accept loop and all are joined at shutdown (40).
