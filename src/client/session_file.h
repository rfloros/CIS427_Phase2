// The saved session (name, code, token) that lets a restarted client RECONNECT.
// It holds a token, so the file is created readable only by its owner.
#ifndef CLIENT_SESSION_FILE_H
#define CLIENT_SESSION_FILE_H

#include <string>

struct Session {
    std::string name;
    std::string code;
    std::string token;
};

bool save_session(const std::string& path, const Session& session);
bool load_session(const std::string& path, Session& session);
void remove_session(const std::string& path);

#endif
