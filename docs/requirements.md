> Copied from the Google Doc tabs "5.3 Functional Req v2" and "5.4 Non-Functional v2". Test IDs refer to test-plan.md.


# 5.3 Functional Requirements (Revision 2)
## TermTrivia
Each requirement has an ID, the protocol section that defines it, the phase in which it can be demonstrated, and the tests that verify it (section 5.8). Phase 2 means it can be demonstrated with one client connected to the server. Phase 3 means it needs several clients connected at the same time.
### Must have
- **FR-01** The client shall let a user create a game by giving a host name and a number of questions (CREATE_GAME) and shall display the join code that the server returns. Protocol 4.1. Phase 2. Tests: T01.
- **FR-02** The client shall let a user join a game with a join code and a name (JOIN_GAME), including after the game has started. Protocol 4.2. Phase 3. Tests: T11.
- **FR-03** The host shall be able to start the game once (START_ROUND). After that, every round and intermission advances automatically on a timer. Protocol 4.3. Phase 2. Tests: T02, T35.
- **FR-04** The client shall display every question pushed by the server as soon as it arrives, even while the user is idle at the prompt. Protocol 5.1 and 2.3. Phase 2. Tests: T02, T34.
- **FR-05** The client shall let a player submit an answer A, B, C, or D (SUBMIT_ANSWER) while a round is open. Protocol 4.4. Phase 2. Tests: T03.
- **FR-06** The server shall score an answer on correctness alone, never on speed. Protocol 1. Phase 2. Tests: T05.
- **FR-07** The server shall close a round when every active player has answered or 20 seconds have passed, whichever comes first, and shall push ROUND_RESULT with the correct answer and the leaderboard. Protocol 4.3 and 5.2. Phase 2. Tests: T03, T04.
- **FR-08** The client shall let a player view the leaderboard at any time while in a game (LEADERBOARD). Protocol 4.5. Phase 2. Tests: T06.
- **FR-09** The server shall end the game automatically after the last question and a 10-second final intermission, and shall push GAME_END with reason COMPLETE. Protocol 4.3 and 5.3. Phase 2. Tests: T07.
- **FR-10** The host shall be able to end the game early (END_GAME). Every player in that game receives GAME_END with reason HOST_ENDED. Other games are not affected. Protocol 4.8. Phase 2. Tests: T08, T38.
- **FR-11** A user shall be able to leave deliberately (QUIT). A QUIT is permanent. If a non-host quits, the game continues. If the host quits, the game ends with reason HOST_QUIT. Protocol 4.7. Phase 2 for the host, Phase 3 for other players. Tests: T09, T10.
- **FR-12** A player who was disconnected unexpectedly, host included, shall be able to reconnect with name, game code, and token within 60 seconds and keep their score. If a round is open, the server shall push the current question again. Protocol 4.6. Phase 2. Tests: T27, T28.
### Error handling
- **FR-13** The server shall reject a join code that does not exist (404). Phase 2. Tests: T17.
- **FR-14** The server shall reject a question count that is missing, not an integer, or outside 1 to the number of loaded questions (423), and a missing host name (300). Phase 2. Tests: T16.
- **FR-15** The server shall reject a name already used in that game (409). Names only need to be unique within one game. Phase 3. Tests: T23, T37.
- **FR-16** The server shall reject any command that needs a game when the sender is not in one (401), for example LEADERBOARD. The client shall show the error. Phase 2. Tests: T18.
- **FR-17** The server shall reject a host-only command from a non-host (402). Phase 3. Tests: T22.
- **FR-18** The server shall answer an empty, unknown, malformed, or over-long (more than 512 bytes) line with 300, and shall keep the connection open. Phase 2. Tests: T14, T15, T20, T21.
- **FR-19** The server shall reject SUBMIT_ANSWER when no round is open (420), a repeat START_ROUND (421), and a second answer in the same round (422). Phase 2. Tests: T19.
- **FR-20** The client shall report a failed connection, an invalid port, invalid user input, and a server that closes the connection, without crashing. Phase 2. Tests: T24, T25, T29, T12.
### Graceful disconnection
- **FR-21** If a client disconnects unexpectedly, the server shall keep the player's name and score for 60 seconds, shall not count the player as active when deciding whether everyone has answered, and shall keep running. Protocol 6.4. Phase 2. Tests: T26.
- **FR-22** If the host disconnects unexpectedly, the game shall continue. The host can reconnect within 60 seconds and keeps host powers. Protocol 1. Phase 2. Tests: T30.
### Shared server data
- **FR-23** The server shall keep a separate score for every player in every game, and shall support several games at once without one affecting another. Phase 3. Tests: T36, T38.
### Protocol handling and logging
- **FR-24** The server and the client shall rebuild complete messages from any number of partial or combined reads, without relying on delays and without assuming one send equals one receive. Protocol 6.2. Phase 2. Tests: T31, T32, T33, T34, T35, T39.
- **FR-25** The server shall write a timestamped log of startup, listening address and port, client connections, request types and outcomes, protocol errors, disconnections, and shutdown. Phase 2. Tests: T13.
### Could have (not planned for Phase 2)
- Users could send chat messages in and out of a game.
- The host could choose from several quiz types, such as math and trivia.
- The host could choose scoring rules, such as first correct answer only or any correct answer within the time limit.
- The host could set the time for each question.
- The host could choose team mode or solo mode.
### Changes from the Phase 1 version
- Removed "create a username" and "change a username". A player's name is now given in CREATE_GAME or JOIN_GAME and stays fixed for that game. The protocol has no separate username command, and names only need to be unique inside one game.
- "Leave a room" is now QUIT, which ends the session permanently.
- Removed "if the host disconnects, another host is automatically chosen". The protocol never defined host transfer. Instead the host can reconnect (FR-22), and a deliberate host QUIT ends the game (FR-11).
- "Reject duplicate names" is now per game (FR-15).
- "Error when viewing the scoreboard outside a game" is now the 401 reply (FR-16).
- Added FR-12 (reconnect), FR-18 to FR-20, FR-24, and FR-25, so the requirements cover everything in the protocol and the Phase 2 grading list.
- Every requirement now has an ID and the tests that verify it.



# 5.4 Nonfunctional Requirements (Revision 2)
## TermTrivia
Each requirement is written so it can be checked. "Verified by" names the tests in section 5.8, or an inspection of the code or documents.
### Performance
- **NFR-01** With up to 20 players on localhost or a local network, the server shall write each PUSH QUESTION to every active player within 1 second of the round opening, shall send the reply to any command within 500 milliseconds, and shall keep round timers accurate to plus or minus 1 second (20-second question, 5-second intermission, 10-second final intermission). Verified by: T02, T03, T04.
### Reliability
- **NFR-02** The server shall keep running, with no crash and no unhandled exception, after any empty, unknown, malformed, or over-long command and after any client disconnects. Verified by: T14, T15, T16, T21, T26.
- **NFR-03** A player who reconnects within 60 seconds shall keep their score. After 60 seconds, or with any wrong detail, RECONNECT shall return the same 410 reply. Verified by: T27, T28.
- **NFR-04** The server and the client shall behave correctly when a message is split across any number of reads, including one byte per read, and when several messages arrive in one read. Verified by: T31, T32, T33, T34.
### Usability
- **NFR-05** When the client connects it shall list all commands, and every prompt that expects input shall show the valid input format. A new user shall be able to find the main commands without reading the source code. Verified by: T12.
- **NFR-06** Invalid user input shall produce a clear message and a new prompt. A push that arrives while the user is typing shall not be lost, and the prompt shall be shown again after it. Verified by: T12.
### Scalability
- **NFR-07** A game shall support 1 to about 20 players. In Phase 3 the server shall support at least 3 clients connected at the same time and several games at once. Verified by: T36, T38, T39 (Phase 3).
### Maintainability
- **NFR-08** The code shall separate these responsibilities into their own modules: network connection, message framing, protocol validation, game logic (scoring, rounds, leaderboard), the round timer, data storage, the client user interface, and logging. Game logic shall not call socket functions directly. Verified by: code inspection.
### Security
- **NFR-09** The server shall treat all client input strictly as data and never run or evaluate it. Lines are limited to 512 bytes. Tokens shall never be shown to the user, included in LEADERBOARD or ROUND_RESULT output, or written to the log. RECONNECT failures shall always return the same generic reply. Verified by: T21, T13, T28.
### Fairness and accessibility
- **NFR-10** Scoring shall depend only on whether the answer is correct, never on when it was submitted, so slower typists and players with higher latency are not penalized. Verified by: T05.
### Portability and configuration
- **NFR-11** The server port and the client host and port shall be set on the command line or in a documented setting. There shall be no hard-coded machine-specific paths or addresses. The README shall give the build and run steps. Verified by: README review and a build on a clean machine.
