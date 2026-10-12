> Copied from the Google Doc tab "5.8 Testing Plan v2". Fill in the Actual result and Pass/Fail columns when tests are run. Tests marked Phase 3 are written now and run in Phase 3.

# 5.8 Testing Plan (Revision 2)

## Normal-operation tests

Each test checks that a normal feature works as specified.

| Test ID | Requirement tested | Initial conditions | Input or action | Expected result | Actual result | Pass/Fail |
|---|---|---|---|---|---|---|
| T01 | FR-01 Create a game | Server running. Client connected. Not in a game. | CREATE_GAME Robert 3 | 200 OK, then a 4-character join code, then a 4-character token, then a blank line. The client shows the join code and does not show the token. | Not run yet | Pending |
| T02 | FR-03, FR-04, NFR-01 Start the game and receive the first question | Host has created a game with 3 questions. | START_ROUND | The 200 OK reply arrives first. Then PUSH QUESTION 1 with options A to D is displayed within 1 second, with no user input. | Not run yet | Pending |
| T03 | FR-05, FR-07 Submit an answer and close the round early | Question 1 is open. The host is the only active player. | SUBMIT_ANSWER B | 200 OK. Because every active player has answered, PUSH ROUND_RESULT 1 follows at once with the correct answer and the leaderboard, without waiting 20 seconds. | Not run yet | Pending |
| T04 | FR-07, NFR-01 Round closes on the timer | A question is open. No answer is submitted. | Wait | PUSH ROUND_RESULT arrives 20 seconds (plus or minus 1) after the question, with the score unchanged. The next PUSH QUESTION arrives 5 seconds (plus or minus 1) later. | Not run yet | Pending |
| T05 | FR-06, NFR-10 Scoring ignores speed | Two separate games, each with a question open. | In one game answer correctly after 1 second. In the other answer correctly after 18 seconds. | In both games the player gains exactly 1 point. | Not run yet | Pending |
| T06 | FR-08 View the leaderboard | In a game, after at least one round has closed. | LEADERBOARD | 200 OK, then one name:score line per player sorted highest first, then a blank line. | Not run yet | Pending |
| T07 | FR-09 Game ends automatically | The final question's round has just closed. | Wait | After a 10-second intermission, PUSH GAME_END COMPLETE with FINAL RESULTS arrives and the connection closes. The server keeps listening. | Not run yet | Pending |
| T08 | FR-10 Host ends the game early | Game running with a question open. | END_GAME | 200 OK, then PUSH GAME_END HOST_ENDED, then the connection closes. No further QUESTION pushes arrive. The server accepts new connections. | Not run yet | Pending |
| T09 | FR-11 Host quits | Host is in a game. | QUIT | 200 OK, then the connection closes and the game is removed. Any other players receive PUSH GAME_END HOST_QUIT. RECONNECT with the old token returns 410. | Not run yet | Pending |
| T10 | FR-11 Non-host quits (Phase 3) | Host and one player are in a game. | The player sends QUIT | The player gets 200 OK and is disconnected. The game continues for the host. The player no longer appears in LEADERBOARD. | Not run yet | Pending |
| T11 | FR-02 Join a game in the lobby (Phase 3) | A game is in LOBBY. A second client is connected. | JOIN_GAME <code> Matthew | 200 OK, then '2 players in game <code>', then a token, then a blank line. | Not run yet | Pending |
| T12 | FR-20, NFR-05, NFR-06 Client shows commands and input formats | Client started with a valid host and port. | Observe the startup screen and each prompt. Enter an invalid menu choice. | All commands are listed on connect. Each prompt shows its valid input format. The invalid choice gives a message and a new prompt, not an exit. | Not run yet | Pending |
| T13 | FR-25, NFR-09 Server log contents | Server started. Tests above run in one session. | Read server.log | Timestamped lines for startup, listening address and port, connect (with client id), each request type and outcome, errors, disconnect, and shutdown. No token appears anywhere. | Not run yet | Pending |

## Invalid-input tests

Each test checks that bad input is rejected with the right code and that the connection stays usable.

| Test ID | Requirement tested | Initial conditions | Input or action | Expected result | Actual result | Pass/Fail |
|---|---|---|---|---|---|---|
| T14 | FR-18 Empty line | Client connected. | Send a blank line | 300 message format error. The connection stays open and the next valid command works. | Not run yet | Pending |
| T15 | FR-18 Unknown command | Client connected. | HELLO | 300 message format error. The connection stays open. | Not run yet | Pending |
| T16 | FR-14 Bad CREATE_GAME arguments | Not in a game. The server has N questions loaded. | CREATE_GAME Robert abc, then CREATE_GAME Robert 0, then CREATE_GAME Robert 9999, then CREATE_GAME | The first three return 423 with the range 1 to N. The last returns 300. No game is created. | Not run yet | Pending |
| T17 | FR-13 Unknown join code | Client connected. Not in a game. | JOIN_GAME ZZZZ Matthew | 404 No game exists with that code. The client can try again. | Not run yet | Pending |
| T18 | FR-16 Command that needs a game | Client connected. Not in a game. | LEADERBOARD, then SUBMIT_ANSWER A, then START_ROUND | Each returns 401 You are not currently in a game, join or reconnect first. | Not run yet | Pending |
| T19 | FR-19 Command in the wrong game state | Host in a game that is still in LOBBY. | SUBMIT_ANSWER A; then START_ROUND twice; then SUBMIT_ANSWER twice in one round | 420 for the first answer. 421 for the second START_ROUND. 422 for the second answer in the round. | Not run yet | Pending |
| T20 | FR-18 Invalid answer choice | A question is open. | SUBMIT_ANSWER E | 300 message format error. The player can still submit A to D afterwards. | Not run yet | Pending |
| T21 | FR-18, NFR-09 Over-length line | Client connected. | Send one line of 600 characters, then LEADERBOARD | 300 for the long line. Input up to its newline is discarded. The next command is handled normally. No crash. | Not run yet | Pending |
| T22 | FR-17 Non-host uses a host command (Phase 3) | Host and a player are in a game. | The player sends END_GAME | 402 Only the host may execute this command. The game continues. | Not run yet | Pending |
| T23 | FR-15 Duplicate name in a game (Phase 3) | A game already has a player named Matthew. | Another client sends JOIN_GAME <code> Matthew | 409 That name is already in use in this game. The client can choose another name. | Not run yet | Pending |

## Networking failure tests

Each test checks that a connection failure is handled without a crash.

| Test ID | Requirement tested | Initial conditions | Input or action | Expected result | Actual result | Pass/Fail |
|---|---|---|---|---|---|---|
| T24 | FR-20 Server not running | No server is listening. | Start the client with a host and port | A clear message such as 'Could not connect to host:port', exit with a non-zero status, no unhandled exception. | Not run yet | Pending |
| T25 | FR-20 Invalid port | Client not started. | Start the client with port abc, then 0, then 70000 | An error or usage message for each. The client exits without attempting a connection. | Not run yet | Pending |
| T26 | FR-21 Client exits unexpectedly | Client is in a running game. | Kill the client process | The server logs the disconnect and keeps listening. The player becomes DISCONNECTED_PENDING for 60 seconds. No crash. | Not run yet | Pending |
| T27 | FR-12, NFR-03 Reconnect within the window | After the previous test, less than 60 seconds have passed. | RECONNECT Robert <code> <token> from a new client | 200 OK, then '<score> <question number>'. If a round is live, the current question is pushed again. The score is unchanged. | Not run yet | Pending |
| T28 | FR-12, NFR-03, NFR-09 Reconnect rejected | A pending session exists. | RECONNECT with (a) a wrong token, (b) a wrong name, (c) correct details after 60 seconds | All three return the identical 410 reply. | Not run yet | Pending |
| T29 | FR-20 Server stops while the client is active | Client is waiting at its prompt. | Stop the server | The client prints that the server closed the connection and exits cleanly. No crash. | Not run yet | Pending |
| T30 | FR-22 Host disconnect does not end the game | Game running. | Kill the host client, wait 25 seconds, then RECONNECT the host | The game keeps running. The host reconnects with host powers. END_GAME is still accepted. | Not run yet | Pending |

## Protocol-framing tests

Each test checks that messages are rebuilt correctly however the bytes arrive.

| Test ID | Requirement tested | Initial conditions | Input or action | Expected result | Actual result | Pass/Fail |
|---|---|---|---|---|---|---|
| T31 | FR-24, NFR-04 Command split across reads | Scripted raw-socket client connected. | Send 'CREATE_GAME Rob', wait 500 ms, then send 'ert 3' and a newline | Exactly one 200 OK reply for CREATE_GAME Robert 3. No 300 error. | Not run yet | Pending |
| T32 | FR-24, NFR-04 Two commands in one send | In a game. Scripted client. | Send 'LEADERBOARD' newline 'LEADERBOARD' newline in a single send | Two complete replies, in order, each ending with a blank line. | Not run yet | Pending |
| T33 | FR-24, NFR-04 One byte at a time | Scripted client connected. | Send CREATE_GAME Robert 3 and a newline one byte per send, 10 ms apart | The same reply as the create test. | Not run yet | Pending |
| T34 | FR-04, FR-24 Client reassembles split server messages | A test server sends bytes in awkward chunks: 1 byte at a time, a reply and a push together, a push between a command and its reply. | The client sends a command | Each push is printed once and in full. The reply goes back to the prompt. No push is ever treated as the command's reply. | Not run yet | Pending |
| T35 | FR-03, FR-24 Reply written before pushes | A game in LOBBY. | START_ROUND, repeated 50 times on fresh games | In every run the 200 OK reply is received before PUSH QUESTION 1. | Not run yet | Pending |

## Concurrency tests planned for Phase 3

These tests are written now and run in Phase 3.

| Test ID | Requirement tested | Initial conditions | Input or action | Expected result | Actual result | Pass/Fail |
|---|---|---|---|---|---|---|
| T36 | FR-23 Simultaneous answers | A question is open with 3 or more players. | All players submit an answer at the same moment (scripted) | Every answer is counted once. Scores are correct. No crash. | Not run yet | Pending |
| T37 | FR-15 Simultaneous joins with the same name | A game in LOBBY. Two clients ready. | Both send JOIN_GAME <code> Matthew at the same time | One gets 200 OK and the other gets 409. | Not run yet | Pending |
| T38 | FR-23 Games are isolated | Two games are running. | The host of game A sends END_GAME | Game A ends. Game B continues unchanged. | Not run yet | Pending |
| T39 | NFR-07, FR-24 Messages are never interleaved | Rounds open and close while clients send commands. | Many clients send LEADERBOARD repeatedly during a running game | No client sees lines from two messages mixed together. Every message ends with a blank line. | Not run yet | Pending |
