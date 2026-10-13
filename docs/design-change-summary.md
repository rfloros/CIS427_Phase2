# TermTrivia Phase 2: design-change summary

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

## Implementation decisions

- **Configurable timers.** Command-line options override 20/5/10/60 s so tests run fast; the spec
  values stay the defaults, and overrides must be positive (13, 28).
- **Question bank format.** One question per line, `|`-separated, at least one valid question
  required, malformed lines skipped with a warning (14).
- **Early round close** needs at least one ACTIVE player; with none, the timer decides (15). A game
  whose players have all timed out is removed (16).
- **Delivery.** One coordinator mutex plus direct, bounded writes (200 ms send timeout) instead of a
  writer thread and queue; this keeps reply-before-push simple and explainable (31). Ctrl+C uses a
  wake-up pipe (32). After an intentional close the server waits up to 0.5 s for the client so the
  final message is not lost (33).
- **Client.** Saved 0600 session file for RECONNECT (17); Ctrl+D closes without QUIT and keeps the
  session (34); `help` and bare RECONNECT are local, command words are uppercased (35); GAME_END and
  server close end the client cleanly (36).
- **Testing.** Our own `tests/minitest.h` and shell scripts instead of doctest (20). `protocol.*`
  lives in `src/common/` because the client reuses it (37).
- **Phase 2 scope.** One client at a time (18); game logic returns effects and never touches
  sockets (19), so Phase 3 only has to run the existing handler on a thread per client.
