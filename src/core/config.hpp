#pragma once

#include <string>

namespace mustela {

struct Settings {
    std::string homepage = "https://example.org";
    std::string search_template = "https://duckduckgo.com/?q=%s";
    int window_width = 1280;
    int window_height = 800;
    bool private_tabs_enabled = true;
    bool start_in_private = false;
    bool history_enabled = true;
};

Settings load_settings(const std::string& path);
void save_settings(const std::string& path, const Settings& settings);

}
