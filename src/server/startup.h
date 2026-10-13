// Everything main() does after the arguments are parsed: open the log, load
// questions, choose the seed, start the server, and run until stopped.
#ifndef SERVER_STARTUP_H
#define SERVER_STARTUP_H

#include <ostream>

#include "server/config.h"
#include "server/random_seed.h"

// Returns the process exit code (0 after a clean stop, 1 if startup failed).
// Problems are written to the log and to `error_out`.
int run_server(const ServerConfig& config, const EntropyFn& entropy, std::ostream& error_out);

// Asks the running server to stop. Safe to call from a signal handler.
void stop_running_server();

#endif
