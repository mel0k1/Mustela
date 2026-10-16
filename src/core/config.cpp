#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <map>

namespace mustela {

namespace {

std::string trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool to_bool(const std::string& value, bool fallback) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
    if (value == "false" || value == "0" || value == "no" || value == "off") return false;
    return fallback;
}

int to_int(const std::string& value, int fallback) {
    char* end = nullptr;
    long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) return fallback;
    return static_cast<int>(parsed);
}

double to_double(const std::string& value, double fallback) {
    char* end = nullptr;
    double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) return fallback;
    return parsed;
}

std::map<std::string, std::string> parse_toml_subset(const std::string& path) {
    std::map<std::string, std::string> pairs;
    std::ifstream in(path);
    if (!in) return pairs;

    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        std::string text = trim(line);
        if (text.empty() || text[0] == '#') continue;

        if (text.front() == '[' && text.back() == ']') {
            section = trim(text.substr(1, text.size() - 2));
            continue;
        }

        size_t split = text.find('=');
        if (split == std::string::npos) continue;

        std::string key = trim(text.substr(0, split));
        std::string value = trim(text.substr(split + 1));
        if (value.empty()) continue;

        if (value.front() == '"') {
            size_t closing = value.find('"', 1);
            if (closing == std::string::npos) continue;
            value = value.substr(1, closing - 1);
        } else {
            size_t comment = value.find('#');
            if (comment != std::string::npos) value = trim(value.substr(0, comment));
        }

        if (!section.empty()) key = section + "." + key;
        pairs[key] = value;
    }
    return pairs;
}

void apply(const std::map<std::string, std::string>& pairs, const std::string& key, std::string& target) {
    auto it = pairs.find(key);
    if (it != pairs.end() && !it->second.empty()) target = it->second;
}

void apply(const std::map<std::string, std::string>& pairs, const std::string& key, int& target) {
    auto it = pairs.find(key);
    if (it != pairs.end()) target = to_int(it->second, target);
}

void apply(const std::map<std::string, std::string>& pairs, const std::string& key, bool& target) {
    auto it = pairs.find(key);
    if (it != pairs.end()) target = to_bool(it->second, target);
}

void apply(const std::map<std::string, std::string>& pairs, const std::string& key, double& target) {
    auto it = pairs.find(key);
    if (it != pairs.end()) target = to_double(it->second, target);
}

double clamp_zoom(double zoom) {
    if (zoom < 0.3) return 0.3;
    if (zoom > 3.0) return 3.0;
    return zoom;
}

std::string permission_value(const std::map<std::string, std::string>& pairs, const std::string& key,
    const std::string& fallback) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return fallback;
    const std::string& value = it->second;
    if (value == "deny" || value == "ask" || value == "allow") return value;
    return fallback;
}

std::string downloads_mode_value(const std::map<std::string, std::string>& pairs, const std::string& key,
    const std::string& fallback) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return fallback;
    const std::string& value = it->second;
    if (value == "auto" || value == "ask") return value;
    return fallback;
}

}

Settings load_settings(const std::string& path) {
    auto pairs = parse_toml_subset(path);

    Settings settings;
    apply(pairs, "shell.homepage", settings.homepage);
    apply(pairs, "shell.search_template", settings.search_template);
    apply(pairs, "shell.window_width", settings.window_width);
    apply(pairs, "shell.window_height", settings.window_height);
    apply(pairs, "privacy.private_tabs_enabled", settings.private_tabs_enabled);
    apply(pairs, "privacy.start_in_private", settings.start_in_private);
    apply(pairs, "privacy.history_enabled", settings.history_enabled);
    apply(pairs, "privacy.cookie_policy", settings.cookie_policy);
    apply(pairs, "privacy.tracking_prevention", settings.tracking_prevention);
    apply(pairs, "privacy.javascript", settings.javascript_enabled);
    apply(pairs, "privacy.hyperlink_auditing", settings.hyperlink_auditing);
    apply(pairs, "privacy.dns_prefetching", settings.dns_prefetching);
    apply(pairs, "privacy.block_autoplay", settings.block_autoplay);
    apply(pairs, "privacy.fingerprint_webgl", settings.fingerprint_webgl);
    apply(pairs, "privacy.fingerprint_canvas", settings.fingerprint_canvas);
    apply(pairs, "privacy.fingerprint_fonts", settings.fingerprint_fonts);
    apply(pairs, "privacy.fingerprint_referrer", settings.fingerprint_referrer);
    apply(pairs, "perf.process_model", settings.process_model);
    apply(pairs, "perf.startup_budget_ms", settings.startup_budget_ms);
    apply(pairs, "perf.memory_ceiling_mb", settings.memory_ceiling_mb);
    apply(pairs, "perf.log", settings.perf_log);
    apply(pairs, "downloads.dir", settings.downloads_dir);
    settings.downloads_mode = downloads_mode_value(pairs, "downloads.mode", settings.downloads_mode);
    double zoom = settings.zoom_default;
    apply(pairs, "shell.zoom", zoom);
    settings.zoom_default = clamp_zoom(zoom);
    settings.permissions_geolocation = permission_value(pairs, "permissions.geolocation", settings.permissions_geolocation);
    settings.permissions_camera = permission_value(pairs, "permissions.camera", settings.permissions_camera);
    settings.permissions_microphone = permission_value(pairs, "permissions.microphone", settings.permissions_microphone);
    settings.permissions_notifications = permission_value(pairs, "permissions.notifications", settings.permissions_notifications);
    apply(pairs, "security.tls_strict", settings.tls_strict);
    return settings;
}

void save_settings(const std::string& path, const Settings& settings) {
    std::ofstream out(path);
    if (!out) return;

    out << "[shell]\n";
    out << "homepage = \"" << settings.homepage << "\"\n";
    out << "search_template = \"" << settings.search_template << "\"\n";
    out << "window_width = " << settings.window_width << "\n";
    out << "window_height = " << settings.window_height << "\n";
    out << "zoom = " << clamp_zoom(settings.zoom_default) << "\n";
    out << "\n[privacy]\n";
    out << "private_tabs_enabled = " << (settings.private_tabs_enabled ? "true" : "false") << "\n";
    out << "start_in_private = " << (settings.start_in_private ? "true" : "false") << "\n";
    out << "history_enabled = " << (settings.history_enabled ? "true" : "false") << "\n";
    out << "cookie_policy = \"" << settings.cookie_policy << "\"\n";
    out << "tracking_prevention = " << (settings.tracking_prevention ? "true" : "false") << "\n";
    out << "javascript = " << (settings.javascript_enabled ? "true" : "false") << "\n";
    out << "hyperlink_auditing = " << (settings.hyperlink_auditing ? "true" : "false") << "\n";
    out << "dns_prefetching = " << (settings.dns_prefetching ? "true" : "false") << "\n";
    out << "block_autoplay = " << (settings.block_autoplay ? "true" : "false") << "\n";
    out << "fingerprint_webgl = " << (settings.fingerprint_webgl ? "true" : "false") << "\n";
    out << "fingerprint_canvas = " << (settings.fingerprint_canvas ? "true" : "false") << "\n";
    out << "fingerprint_fonts = " << (settings.fingerprint_fonts ? "true" : "false") << "\n";
    out << "fingerprint_referrer = \"" << settings.fingerprint_referrer << "\"\n";
    out << "\n[perf]\n";
    out << "process_model = \"" << settings.process_model << "\"\n";
    out << "startup_budget_ms = " << settings.startup_budget_ms << "\n";
    out << "memory_ceiling_mb = " << settings.memory_ceiling_mb << "\n";
    out << "log = " << (settings.perf_log ? "true" : "false") << "\n";
    out << "\n[downloads]\n";
    out << "dir = \"" << settings.downloads_dir << "\"\n";
    out << "mode = \"" << settings.downloads_mode << "\"\n";
    out << "\n[permissions]\n";
    out << "geolocation = \"" << settings.permissions_geolocation << "\"\n";
    out << "camera = \"" << settings.permissions_camera << "\"\n";
    out << "microphone = \"" << settings.permissions_microphone << "\"\n";
    out << "notifications = \"" << settings.permissions_notifications << "\"\n";
    out << "\n[security]\n";
    out << "tls_strict = " << (settings.tls_strict ? "true" : "false") << "\n";
}

}
