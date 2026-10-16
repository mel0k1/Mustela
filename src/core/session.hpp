#pragma once

#include <string>
#include <vector>

namespace mustela {

struct SessionTab {
    std::string url;
    std::string title;
    std::vector<std::string> back;
    std::vector<std::string> forward;
    std::string state;
};

struct SessionData {
    std::vector<SessionTab> tabs;
    int active = 0;
};

bool load_session(const std::string& path, SessionData& out);
bool save_session(const std::string& path, const SessionData& data);

}
