// Where the server's random seed comes from.
#ifndef SERVER_RANDOM_SEED_H
#define SERVER_RANDOM_SEED_H

#include <cstdint>
#include <functional>

// Fills `seed` and returns true, or returns false if no entropy is available.
using EntropyFn = std::function<bool(uint64_t& seed)>;

// Reads Linux OS entropy (getrandom).
bool os_entropy(uint64_t& seed);

#endif
