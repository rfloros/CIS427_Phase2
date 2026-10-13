#include "server/random_seed.h"

#include <sys/random.h>

#include <cerrno>

bool os_entropy(uint64_t& seed) {
    while (true) {
        const ssize_t got = getrandom(&seed, sizeof(seed), 0);
        if (got == static_cast<ssize_t>(sizeof(seed))) return true;
        if (got < 0 && errno == EINTR) continue;
        return false;
    }
}
