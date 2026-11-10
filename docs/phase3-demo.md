# TermTrivia Phase 3 demonstration guide

The final demonstration is on **Thursday, November 12th, between 12 and 1:45 PM**. This guide shows each
required item, the exact command, what to point at, and a proposed speaker. **Every speaking part below is a
proposal for Robert and Matthew to confirm or change.** Nothing here records who actually did what.

## Before the demo

```sh
make clean && make all && make test      # about 1 minute; all checks must pass
bash scripts/demo_phase3.sh              # rehearse the scripted run once (about 20 seconds)
```

Two ways to demonstrate. **Live (recommended for questions):** open four or five terminals, start
`./server 5000 --seed 7` in one and `./client localhost 5000` in the others. **Scripted (repeatable):**
`bash scripts/demo_phase3.sh`, which plays the same story with raw-socket clients and prints what each client
saw, then the server log. The scripted run uses a demo bank (`scripts/demo_questions.txt`) whose correct answer
is always A, and short timers (4 s question, 1 s intermission, 2 s final, 30 s reconnect window).

## Server startup

- **Live:** `./server 5000` in terminal 1. It prints `TermTrivia server listening on port 5000`.
- **Scripted:** step 1 prints the first two log lines (`server starting`, `listening on 0.0.0.0:PORT`).
- **Say:** one process, a listening socket, one thread per client and one timer thread per game.

## Three simultaneous client connections

- **Live:** start `./client localhost 5000` in terminals 2, 3, and 4 and leave all of them connected.
- **Scripted:** steps 2 and 3 connect Robert, Matthew, Alice, Bob, and two Sam racers at once (six connections).
- **Say:** the Phase 2 server made a second client wait in the listen backlog; now every client has a handler.

## Client identities and session information

- **Live:** in terminal 2 `create_game Robert 3` prints the join code. In 3 and 4 `join_game CODE Matthew` and
  `join_game CODE Alice`. Show `ls -l .termtrivia_session` (mode 600) in one client folder.
- **Scripted:** the "What the clients saw" section shows each name, its `N players in game CODE` reply, and
  `<private token>` where the secret is hidden. The server log lines `client N connected from ...` map client
  numbers to sockets.
- **Say:** the name and join code are public; the private token is what proves identity on RECONNECT and is never
  displayed or logged.

## Shared application state

- **Live:** the host types `start_round`. All players see QUESTION 1 at the same moment. Any player types
  `leaderboard` and sees the same scores.
- **Scripted:** every client's output shows the same QUESTION and ROUND_RESULT text and the same standings.
- **Say:** one game object on the server holds the question, answers, and scores; clients only display it.

## Concurrent user operations

- **Live:** the host, Matthew, and Alice all type an answer within a second of each other; the host also types
  `leaderboard` while others answer.
- **Scripted:** question 1 is answered by Robert, Matthew, and Bob at the same moment (Alice follows two seconds
  later). The log shows the `SUBMIT_ANSWER -> 200` lines from different clients in the same second.
- **Say:** each client has its own thread; the game's lock makes each action happen as one whole step.

## Conflicting or overlapping operation

- **Live:** from two spare terminals, have both type `join_game CODE Sam` at the same time (count to three and
  press Enter together).
- **Scripted:** step 3 starts two clients that send `JOIN_GAME CODE Sam` at once. The output shows one `200 OK`
  and one `409 That name is already in use in this game.`; the log shows `JOIN_GAME -> 200` and `JOIN_GAME -> 409`.
- **Say:** this is the name conflict from the Curiosity Report (question 5), tested as T37.

## Correct synchronization and conflict resolution

- **Point at:** exactly one Sam in the leaderboard (never two), scores that match the answers (T36), and the
  rule that a reply is always written before any push to the same client (T35, T39).
- **Say:** each game has one lane (a mutex) held from the state change until its messages are written, so the
  game's outcome and message order are the same as if the actions had run one after another.

## Unexpected client disconnect

- **Live:** in Matthew's terminal press Ctrl+C or close the terminal during a question (the client leaves without
  QUIT).
- **Scripted:** Bob's connection is killed during question 2. The log shows
  `player Bob disconnected, reconnect window open` and `client 3 disconnected: closed by peer`.
- **Say:** an unexpected drop keeps the player for the reconnect window (60 s by default); QUIT would remove them.

## Continued operation for the remaining clients

- **Live:** the other terminals keep receiving questions and results; show another `leaderboard`.
- **Scripted:** questions 2 and 3 still reach Robert, Matthew, and Alice, and round 2 closes as soon as the three
  active players have answered (the log shows `round 2 closed`).
- **Optional:** restart the dropped client folder's client and type `reconnect`; the score is kept (T46). The
  scripted run does this with a second Bob connection that prints `200 OK` and `0 3`.

## Malformed or invalid request

- **Live:** type a command the client lets through to the server, or run
  `build/raw_client 127.0.0.1 5000 send 'HELLO\n' read 300`; the server answers `300 message format error` and the
  connection stays open. Also try `join_game ZZZZ Pat` for `404`.
- **Scripted:** Robert's output shows `HELLO` answered by `300 message format error`; the log shows
  `WARN client 1 protocol error 300`.
- **Say:** every bad line gets exactly one error reply and never crashes or closes the connection.

## Graceful connection termination

- **Live:** let the game finish, or type `quit` in a player terminal, then `end_game` in the host terminal of a
  second game.
- **Scripted:** after the last question, every connected player gets `PUSH GAME_END COMPLETE` with final results
  and the server closes each socket (`session ended by server`). Finally Ctrl+C logs `server shutting down`.
- **Say:** a normal end sends GAME_END and then closes; a host that never returns to a lobby ends it with
  `HOST_EXPIRED`.

## Server logs

- **Live:** `tail -n 40 server.log` in another terminal. **Scripted:** step 6 prints the whole log.
- **Point at:** one timestamped line per connection, request and result code, game event, error, disconnect, and
  shutdown. No private token appears anywhere in it; tokens are never written to the log.
- **Reference:** `docs/sample_server.log` is a saved log of the scripted run.

## Distinctive value-creating feature

- **Say:** TermTrivia scores correctness only, never speed. In the scripted run Alice answers two seconds after
  Matthew and both get the same point. In the Curiosity Report interviews both classmates preferred this over
  speed-based scoring, and it also keeps the game fair for slow typists and high-latency connections.
- **Also true:** no account, no cost, and a join code is all a player needs.

## Scalability discussion

- **Say (about 30 seconds):** one thread per client plus one timer thread per game keeps the code simple and
  fits the target of about 20 players per game. Each game has its own lock, so games do not slow one another; a
  stalled client costs its own game at most one 200 ms write timeout (T40). With 20 players on localhost a reply
  took about 1 ms and a question reached everyone within 2 ms. The limits are thread and memory use per client,
  one process on one machine, and writes done one client at a time inside a game; thousands of clients would call
  for an event loop or a thread pool and a writer queue. Timed play was measured with 20 players; a load test held 210 clients in the lobby with no failed request.
- **Reference:** the scalability analysis in `docs/phase3-technical-report.pdf`.

## Proposed speaking parts (Robert and Matthew to confirm)

| Part | Proposed speaker | Notes |
|---|---|---|
| Server startup, connections, identities | Robert | to confirm |
| Shared state, concurrent operations | Matthew | to confirm |
| Conflicting operation and synchronization | Robert | to confirm |
| Disconnect, continued operation, reconnect | Matthew | to confirm |
| Invalid request, termination, logs | Robert | to confirm |
| Distinctive feature and scalability | Matthew | to confirm |

Whoever is not speaking drives the terminals. These are suggestions only; the team decides and may swap any part.
