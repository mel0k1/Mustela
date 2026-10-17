#pragma once

#include <string>
#include <vector>

namespace mustela {

constexpr int kSettingsVersion = 1;

struct SettingsReport {
    int version = 0;
    int applied = 0;
    bool file_found = false;
    std::vector<std::string> unknown_keys;
    std::vector<std::string> invalid_values;
    std::vector<std::string> warnings;

    std::string summary() const;
};

struct Settings {
    std::string homepage = "https://example.org";
    std::string search_template = "https://duckduckgo.com/?q=%s";
    int window_width = 1280;
    int window_height = 800;
    bool private_tabs_enabled = true;
    bool start_in_private = false;
    bool history_enabled = true;
    std::string cookie_policy = "no-third-party";
    bool tracking_prevention = true;
    bool javascript_enabled = true;
    bool hyperlink_auditing = false;
    bool dns_prefetching = false;
    bool block_autoplay = true;
    bool fingerprint_webgl = false;
    bool fingerprint_canvas = false;
    bool fingerprint_fonts = false;
    std::string fingerprint_referrer = "same-origin";
    std::string process_model = "per-view";
    int startup_budget_ms = 0;
    int memory_ceiling_mb = 0;
    bool perf_log = true;
    std::string downloads_dir;
    std::string downloads_mode = "auto";
    double zoom_default = 1.0;
    std::string permissions_geolocation = "deny";
    std::string permissions_camera = "deny";
    std::string permissions_microphone = "deny";
    std::string permissions_notifications = "deny";
    bool tls_strict = true;
    bool session_restore = true;
    bool devtools = true;
    bool spellcheck = true;
    std::string spellcheck_languages;
    bool gpc = true;
    bool blocklist = true;
    std::string blocklist_file;
    std::string dns_over_https;
    std::string proxy_mode = "off";
    std::string proxy_upstream;
    std::string proxy_user;
    std::string proxy_password;
};

Settings load_settings(const std::string& path);
Settings load_settings(const std::string& path, SettingsReport& report);
void save_settings(const std::string& path, const Settings& settings);

}
