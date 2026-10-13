#include "client/session_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

bool save_session(const std::string& path, const Session& session) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    // open() keeps the old mode of an existing file, so set it explicitly.
    fchmod(fd, 0600);
    const std::string text = session.name + " " + session.code + " " + session.token + "\n";
    const ssize_t written = write(fd, text.data(), text.size());
    close(fd);
    return written == static_cast<ssize_t>(text.size());
}

bool load_session(const std::string& path, Session& session) {
    std::ifstream file(path);
    if (!file) return false;
    std::string line;
    std::getline(file, line);
    std::istringstream fields(line);
    Session loaded;
    std::string extra;
    if (!(fields >> loaded.name >> loaded.code >> loaded.token) || (fields >> extra)) return false;
    session = loaded;
    return true;
}

void remove_session(const std::string& path) { unlink(path.c_str()); }
