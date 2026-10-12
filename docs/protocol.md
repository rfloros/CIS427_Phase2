> Source of truth for behavior. Copied from the Google Doc tab "5.6 Protocol v2". If the code must differ, log it in design-changes.md first.

# CIS 427 Protocol Specification (Revision 2)
## TermTrivia
## 1. Overview
The TermTrivia protocol is a line-oriented client-server protocol over TCP. All commands and responses are ASCII text, and every line ends with a newline character (\n).
When the server starts, it loads a preloaded set of multiple-choice trivia questions into memory, each with four options (A to D) and one correct answer. The host chooses how many of these questions a game will use when the game is created.
Once the host starts the game, round progression is fully automatic and time-based. Each question is open for 20 seconds, followed by a 5-second intermission showing the leaderboard, then the next question. After the final question's result, the leaderboard stays visible for 10 seconds before the game ends automatically. None of this needs input from the host. After the game starts, the host's only special powers are ending the game early with END_GAME or ending it by deliberately quitting with QUIT.
If the host's connection drops unexpectedly (not a QUIT), the game continues exactly as it would for any other player disconnecting. The host may RECONNECT within the reconnect window and keeps host powers.
**The server must:**
1. Accept a host connection that creates a new game, choosing how many questions the game will use, and return a join code and a private reconnect token.
1. Accept player connections that join an existing game by code, including after the game has started, and return each player their own private reconnect token.
1. Track each player's score and connection state independently.
1. Once the host begins the game, automatically push each question to all active players, close each round on a timer or once everyone has answered, and advance to the next question after a fixed intermission, with no further host input.
1. Score submitted answers on correctness alone, never speed.
1. Push round results and an updated leaderboard after each round closes.
1. End the game automatically after the selected number of questions, or earlier if the host ends it with END_GAME or deliberately quits.
1. Allow a disconnected player (host included) to reconnect with name, game code, and private token, and resume with their score intact within a limited time window.
1. Support multiple simultaneous clients and multiple simultaneous games.
The client must allow the user to issue any of these commands: CREATE_GAME, JOIN_GAME, START_ROUND (host only), SUBMIT_ANSWER, LEADERBOARD, RECONNECT, QUIT, END_GAME (host only). After executing a command, the client returns to its command prompt unless the command ends the client or server connection.
## 2. Message Format
### 2.1 Client to server
A client message is exactly one line ending in \n: the command name followed by its arguments, separated by single spaces. Clients do not send a trailing blank line. An empty line from a client is answered with 300 message format error.
### 2.2 Server to client
Every server message, without exception, is one or more lines followed by one blank line (an empty line, so the message ends with two consecutive newlines). A receiver reads lines until it sees an empty line, which marks the end of one message. Because of this, no body line may ever be empty. Names, question text, and options are validated as non-empty.
There are exactly two kinds of server message, told apart by the first line:
- **Reply:** the first line starts with a 3-digit code (for example 200 OK). A reply is the answer to a command this client sent.
- **Push:** the first line starts with the word PUSH. A push is an unsolicited notification and is never a reply.
Rules for replies and pushes:
- Every client command produces exactly one reply, delivered in the same order the commands were sent.
- A push is never counted as a reply.
- A client may receive a push at any time, including between sending a command and receiving its reply.
### 2.3 Client handling of server messages
The client's background reader thread reads every server message. If the first line starts with PUSH, it displays the message immediately. Otherwise the message is a reply, and the thread hands it to the main thread, which is waiting for the reply to its last command.
## 3. Response Codes
These codes appear only as the first line of a reply.
- **200 OK** Sent when the command was accepted or completed successfully.
- **300 message format error** Sent when the command was empty, unknown, malformed, or too long.
- **401 You are not currently in a game, join or reconnect first.** Sent when the command requires an active game session.
- **402 Only the host may execute this command.** Sent when a non-host attempts a host-only command.
- **404 No game exists with that code.** Sent when JOIN_GAME references an unknown code.
- **409 That name is already in use in this game.** Sent when the JOIN_GAME name conflicts with an existing player.
- **410 Wrong name, game code, or token, or the reconnect window has expired.** Sent when RECONNECT fails, always with this same generic text.
- **420 There is no active round to answer.** Sent when SUBMIT_ANSWER arrives with no round in progress, including during intermission.
- **421 The game has already started.** Sent when START_ROUND arrives after the game has begun.
- **422 You have already submitted an answer for this round.** Sent for a duplicate SUBMIT_ANSWER in the same round.
- **423 Requested question count must be between 1 and <available_count>.** Sent when CREATE_GAME asks for fewer than 1 question or more than are loaded.
## 4. Commands
In every example, c: marks a line sent by the client and s: marks a line sent by the server. A bare s: line is the blank line that ends a server message.
### 4.1 CREATE_GAME
Starts a new game, registers the sender as host, and sets how many questions the game will use.
**Request:** CREATE_GAME <host_name> <num_questions>
- host_name: required, 1 to 20 characters, no spaces.
- num_questions: required, an integer from 1 to the number of loaded questions.
**Server behavior:** Validates both fields, generates a unique 4-character join code and a private 4-character alphanumeric reconnect token, registers the host as the first player with score 0, and randomly selects num_questions questions without repeats.
**Success reply:** 200 OK, then the join code, then the token, then the blank line.
**Errors:** 300 if host_name is missing or malformed. 423 if num_questions is missing, not a number, or out of range.
```
c: CREATE_GAME Robert 10
s: 200 OK
s: 7QXA
s: K3M9
s:
```
K3M9 is Robert's private token. The client stores it locally and never displays it.
### 4.2 JOIN_GAME
Lets a player (or the host, using the same command) join an existing game.
**Request:** JOIN_GAME <code> <name>
- code: required, 4-character alphanumeric, case-insensitive.
- name: required, 1 to 20 characters, no spaces.
**Server behavior:** Checks that the code exists and the name is not already used in that game, adds the player with score 0, and generates their private token. If the game is QUESTION_ACTIVE or INTERMISSION, the player is marked WAITING, does not receive the in-progress question, and starts at the next question. If the game is in LOBBY, the player is ACTIVE immediately.
**Success reply:** 200 OK, then <player_count> players in game <code>, then the token, then the blank line.
**Errors:** 300, 404, 409.
```
c: JOIN_GAME 7QXA Matthew
s: 200 OK
s: 2 players in game 7QXA
s: P8L2
s:
```
### 4.3 START_ROUND
Sent only by the host. This is a one-time trigger that begins the game. After it, every question and intermission advances automatically on a timer.
**Request:** START_ROUND
**Server behavior:** Verifies that the sender is in a game, is the host, and that the game is in LOBBY. Moves the game to QUESTION_ACTIVE and sends the host this reply:
```
s: 200 OK
s:
```
The server then pushes the first question to every ACTIVE player, host included, and starts the 20-second round timer. The host's reply is always written before the first push to the host (section 6.3).
```
s: PUSH QUESTION 1 What is the capital of France?
s: A) London
s: B) Paris
s: C) Berlin
s: D) Madrid
s:
```
From this point the server drives everything on its own, independent of any client:
- The round closes, and the server pushes ROUND_RESULT, when every ACTIVE player has answered or the 20-second timer expires, whichever comes first.
- After the result is pushed, the game enters INTERMISSION: 5 seconds if more questions remain, 10 seconds after the final question. SUBMIT_ANSWER is rejected with 420 during intermission.
- When intermission ends, the server pushes the next QUESTION and starts a new 20-second timer. After the final question it instead ends the game automatically (section 5.3, reason COMPLETE).
**Errors:** 401 if not in a game, 402 if not the host, 421 if the game has already started.
### 4.4 SUBMIT_ANSWER
Lets a player answer the current question.
**Request:** SUBMIT_ANSWER <choice>
- choice: required, one of A, B, C, D (case-insensitive).
**Server behavior:** Verifies that the player is ACTIVE in a game, a round is active, and the player has not already answered this round. Records the answer without revealing correctness. Correctness is revealed to everyone at once in ROUND_RESULT.
**Success reply:** 200 OK.
**Errors:** 300, 401, 420, 422.
```
c: SUBMIT_ANSWER B
s: 200 OK
s:
```
### 4.5 LEADERBOARD
Lets a player check the current standings at any time.
**Request:** LEADERBOARD
**Server behavior:** Verifies that the player is in a game and returns current scores sorted from highest to lowest.
**Errors:** 401.
```
c: LEADERBOARD
s: 200 OK
s: Matthew:3
s: Robert:2
s:
```
### 4.6 RECONNECT
Lets a player who was disconnected unexpectedly (host included) resume with their score intact. The join code and names are known to the whole group, so the private token is what proves this is the same player.
**Request:** RECONNECT <name> <code> <token>
**Server behavior:** Looks for a DISCONNECTED_PENDING session matching name, code, and token, within the 60-second reconnect window. If all three match, the server reattaches the connection, restores score and state, and marks the player ACTIVE. A reconnecting host keeps host powers.
**Success reply:** 200 OK, then <current_score> <question_number> (question_number is 0 in the LOBBY), then the blank line.
```
c: RECONNECT Robert 7QXA K3M9
s: 200 OK
s: 2 3
s:
```
If a round is live (QUESTION_ACTIVE), the server pushes the current question again right after the reply, so the player can still answer. A player who had already submitted an answer still gets 422 on a repeat submission.
```
s: PUSH QUESTION 3 Which planet is known as the Red Planet?
s: A) Venus
s: B) Mars
s: C) Jupiter
s: D) Mercury
s:
```
**Failure:** Any mismatch, unknown game, ended game, or expired window returns the same generic reply, so a guesser cannot tell which part was wrong:
```
s: 410 Wrong name, game code, or token, or the reconnect window has expired.
s:
```
### 4.7 QUIT
Ends the client's session deliberately. A QUIT is permanent and is not eligible for RECONNECT.
**Request:** QUIT
**Server behavior:** Sends the reply, closes that client's socket, and removes the player.
```
c: QUIT
s: 200 OK
s:
```
- If a non-host quits, the game continues normally for everyone else.
- If the host quits, the game ends for everyone using the same sequence as END_GAME (reason HOST_QUIT). This prevents a game that nobody can end.
### 4.8 END_GAME
Sent only by the host. Ends the game for everyone immediately at any point after CREATE_GAME: in the lobby, mid-question, during an intermission, or during the final 10-second intermission.
**Request:** END_GAME
**Server behavior:** Verifies that the sender is in a game and is the host (401 if not in a game, 402 if not the host). Sends the host the reply, then pushes PUSH GAME_END to every connected player in that game, host included, closes that game's client sockets, removes the game and its pending reconnect sessions, and keeps listening. Other games are not affected.
```
c: END_GAME
s: 200 OK
s:
```
## 5. Server Pushes
A push is never a reply. Every push starts with the word PUSH.
### 5.1 PUSH QUESTION
Sent to every ACTIVE player when a question opens, and again to a reconnecting player (section 4.6).
```
s: PUSH QUESTION <question_number> <question_text>
s: A) <option A>
s: B) <option B>
s: C) <option C>
s: D) <option D>
s:
```
### 5.2 PUSH ROUND_RESULT
Sent to every ACTIVE player when a round closes. The game enters INTERMISSION, and any WAITING players become ACTIVE. The leaderboard stays visible for 5 seconds, or 10 seconds after the final question. No client command is involved.
```
s: PUSH ROUND_RESULT 3
s: Correct answer: B
s: Robert:2
s: Matthew:3
s:
```
### 5.3 PUSH GAME_END
Sent to every connected player in the game, host included, when the game ends. The server then closes those sockets.
```
s: PUSH GAME_END <reason>
s: FINAL RESULTS
s: Matthew:5
s: Robert:4
s:
```
The reason is one of:
- **COMPLETE** All questions were played (automatic end).
- **HOST_ENDED** The host sent END_GAME.
- **HOST_QUIT** The host deliberately quit.
## 6. General Protocol Requirements
### 6.1 Framing and encoding
1. Client messages are one line ending in \n. Server messages are one or more \n-terminated lines followed by a blank line (section 2.2). A \r before a \n is ignored.
1. Encoding is ASCII only.
1. A line is at most 512 bytes including the \n. If the server receives a longer line, it replies 300 message format error and discards input up to the next \n.
### 6.2 Receive buffering
TCP is a byte stream, so one recv() can return part of a line, exactly one line, or several lines. Both the client and the server keep a receive buffer for each connection and process it as follows:
1. Append every chunk returned by recv() to the connection's buffer.
1. While the buffer contains a \n, remove everything up to and including it as one complete line and process that line.
1. Leave any remaining partial line in the buffer. The next recv() completes it.
1. If the buffer grows past 512 bytes with no \n, treat it as an over-length line (section 6.1).
1. A recv() that returns 0 bytes means the peer closed the connection.
The client also assembles lines into messages: it collects lines until an empty line, then dispatches the complete message as a push or a reply (section 2.3).
### 6.3 Atomic writes
Each message is sent whole under a per-connection write lock, so a push from the timer thread can never be interleaved inside a reply written by a client-handler thread. For a command that produces both a reply and pushes, such as START_ROUND, the reply is written before the pushes to that client.
### 6.4 Sessions and behavior
1. Connection initialization: there is no handshake. The first line a client sends is its first command.
1. Session state: each connection is NOT_JOINED, ACTIVE, WAITING (joined during QUESTION_ACTIVE or INTERMISSION), or DISCONNECTED_PENDING. Each game is LOBBY, QUESTION_ACTIVE, INTERMISSION, or ENDED.
1. Normal disconnection: QUIT permanently removes a player, and no reconnect slot is held. A host QUIT ends the game (section 4.7).
1. Unexpected disconnection: the player becomes DISCONNECTED_PENDING for 60 seconds and may RECONNECT. A disconnected player is not counted as ACTIVE when deciding whether everyone has answered.
1. Invalid messages: an unrecognized, empty, or malformed line returns 300 message format error, and the connection stays open.
1. Resilience: the server keeps running after any invalid or unknown command and after any client disconnects.
1. Versioning: there is a single fixed protocol version with no negotiation.
1. Server-initiated notifications: QUESTION, ROUND_RESULT, and GAME_END are pushed without a client request (a hybrid of request-response and server push), always tagged with PUSH.
1. Case sensitivity: commands and player names are case-sensitive. SUBMIT_ANSWER choices and join codes are case-insensitive.
1. Game ending: a game ends in exactly one of three ways: all questions are played (COMPLETE), the host sends END_GAME (HOST_ENDED), or the host deliberately quits (HOST_QUIT).
1. Server-driven timing: after START_ROUND, a background timer mechanism, not any client command, closes each round, runs each intermission, and triggers the next question or the end of the game.
1. Reconnect security: CREATE_GAME and JOIN_GAME each return a private 4-character token. RECONNECT requires name, code, and token together. Tokens are never shown to the user, included in LEADERBOARD or ROUND_RESULT output, or written to the server log.
