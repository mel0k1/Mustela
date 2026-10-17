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

bool parse_bool(const std::string& value, bool& out) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        out = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        out = false;
        return true;
    }
    return false;
}

bool parse_int(const std::string& value, int& out) {
    char* end = nullptr;
    long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) return false;
    out = static_cast<int>(parsed);
    return true;
}

bool parse_double(const std::string& value, double& out) {
    char* end = nullptr;
    double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) return false;
    out = parsed;
    return true;
}

int to_int(const std::string& value, int fallback) {
    int out = fallback;
    parse_int(value, out);
    return out;
}

std::map<std::string, std::string> parse_toml_subset(const std::string& path, SettingsReport* report) {
    std::map<std::string, std::string> pairs;
    std::ifstream in(path);
    if (report) report->file_found = static_cast<bool>(in);
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

void apply(std::map<std::string, std::string>& pairs, const std::string& key, std::string& target,
    SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return;
    std::string value = it->second;
    pairs.erase(it);
    if (report) ++report->applied;
    if (!value.empty()) target = value;
}

void apply(std::map<std::string, std::string>& pairs, const std::string& key, int& target,
    SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return;
    int parsed = 0;
    if (!parse_int(it->second, parsed)) {
        pairs.erase(it);
        if (report) report->invalid_values.push_back(key);
        return;
    }
    pairs.erase(it);
    target = parsed;
    if (report) ++report->applied;
}

void apply(std::map<std::string, std::string>& pairs, const std::string& key, bool& target,
    SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return;
    bool parsed = false;
    if (!parse_bool(it->second, parsed)) {
        pairs.erase(it);
        if (report) report->invalid_values.push_back(key);
        return;
    }
    pairs.erase(it);
    target = parsed;
    if (report) ++report->applied;
}

void apply(std::map<std::string, std::string>& pairs, const std::string& key, double& target,
    SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return;
    double parsed = 0;
    if (!parse_double(it->second, parsed)) {
        pairs.erase(it);
        if (report) report->invalid_values.push_back(key);
        return;
    }
    pairs.erase(it);
    target = parsed;
    if (report) ++report->applied;
}

double clamp_zoom(double zoom) {
    if (zoom < 0.3) return 0.3;
    if (zoom > 3.0) return 3.0;
    return zoom;
}

std::string permission_value(std::map<std::string, std::string>& pairs, const std::string& key,
    const std::string& fallback, SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return fallback;
    std::string value = it->second;
    pairs.erase(it);
    if (value == "deny" || value == "ask" || value == "allow") {
        if (report) ++report->applied;
        return value;
    }
    if (report) report->invalid_values.push_back(key);
    return fallback;
}

std::string proxy_mode_value(std::map<std::string, std::string>& pairs, const std::string& key,
    const std::string& fallback, SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return fallback;
    std::string value = it->second;
    pairs.erase(it);
    if (value == "off" || value == "local" || value == "upstream") {
        if (report) ++report->applied;
        return value;
    }
    if (report) report->invalid_values.push_back(key);
    return fallback;
}

std::string memory_action_value(std::map<std::string, std::string>& pairs, const std::string& key,
    const std::string& fallback, SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return fallback;
    std::string value = it->second;
    pairs.erase(it);
    if (value == "warn" || value == "discard") {
        if (report) ++report->applied;
        return value;
    }
    if (report) report->invalid_values.push_back(key);
    return fallback;
}

std::string downloads_mode_value(std::map<std::string, std::string>& pairs, const std::string& key,
    const std::string& fallback, SettingsReport* report) {
    auto it = pairs.find(key);
    if (it == pairs.end()) return fallback;
    std::string value = it->second;
    pairs.erase(it);
    if (value == "auto" || value == "ask") {
        if (report) ++report->applied;
        return value;
    }
    if (report) report->invalid_values.push_back(key);
    return fallback;
}

}

std::string SettingsReport::summary() const {
    return "version=" + std::to_string(version) + " applied=" + std::to_string(applied)
        + " unknown=" + std::to_string(unknown_keys.size())
        + " invalid=" + std::to_string(invalid_values.size());
}

Settings load_settings(const std::string& path) {
    SettingsReport ignored;
    return load_settings(path, ignored);
}

Settings load_settings(const std::string& path, SettingsReport& report) {
    std::map<std::string, std::string> pairs = parse_toml_subset(path, &report);

    auto version = pairs.find("version");
    if (version != pairs.end()) {
        report.version = to_int(version->second, -1);
        pairs.erase(version);
        if (report.version < 0) {
            report.invalid_values.push_back("version");
            report.version = 0;
        } else if (report.version > kSettingsVersion) {
            report.warnings.push_back("settings version " + std::to_string(report.version)
                + " is newer than supported " + std::to_string(kSettingsVersion));
        }
    }

    Settings settings;
    apply(pairs, "shell.homepage", settings.homepage, &report);
    apply(pairs, "shell.search_template", settings.search_template, &report);
    apply(pairs, "shell.profile", settings.shell_profile, &report);
    apply(pairs, "search.default", settings.search_default, &report);
    apply(pairs, "shell.window_width", settings.window_width, &report);
    apply(pairs, "shell.window_height", settings.window_height, &report);
    apply(pairs, "privacy.private_tabs_enabled", settings.private_tabs_enabled, &report);
    apply(pairs, "privacy.start_in_private", settings.start_in_private, &report);
    apply(pairs, "privacy.history_enabled", settings.history_enabled, &report);
    apply(pairs, "privacy.cookie_policy", settings.cookie_policy, &report);
    apply(pairs, "privacy.tracking_prevention", settings.tracking_prevention, &report);
    apply(pairs, "privacy.javascript", settings.javascript_enabled, &report);
    apply(pairs, "privacy.hyperlink_auditing", settings.hyperlink_auditing, &report);
    apply(pairs, "privacy.dns_prefetching", settings.dns_prefetching, &report);
    apply(pairs, "privacy.block_autoplay", settings.block_autoplay, &report);
    apply(pairs, "privacy.fingerprint_webgl", settings.fingerprint_webgl, &report);
    apply(pairs, "privacy.fingerprint_canvas", settings.fingerprint_canvas, &report);
    apply(pairs, "privacy.fingerprint_fonts", settings.fingerprint_fonts, &report);
    apply(pairs, "privacy.fingerprint_referrer", settings.fingerprint_referrer, &report);
    apply(pairs, "perf.process_model", settings.process_model, &report);
    apply(pairs, "perf.startup_budget_ms", settings.startup_budget_ms, &report);
    apply(pairs, "perf.memory_ceiling_mb", settings.memory_ceiling_mb, &report);
    settings.memory_action = memory_action_value(pairs, "perf.memory_action", settings.memory_action, &report);
    apply(pairs, "perf.log", settings.perf_log, &report);
    apply(pairs, "downloads.dir", settings.downloads_dir, &report);
    settings.downloads_mode = downloads_mode_value(pairs, "downloads.mode", settings.downloads_mode, &report);
    double zoom = settings.zoom_default;
    apply(pairs, "shell.zoom", zoom, &report);
    settings.zoom_default = clamp_zoom(zoom);
    settings.permissions_geolocation =
        permission_value(pairs, "permissions.geolocation", settings.permissions_geolocation, &report);
    settings.permissions_camera =
        permission_value(pairs, "permissions.camera", settings.permissions_camera, &report);
    settings.permissions_microphone =
        permission_value(pairs, "permissions.microphone", settings.permissions_microphone, &report);
    settings.permissions_notifications =
        permission_value(pairs, "permissions.notifications", settings.permissions_notifications, &report);
    apply(pairs, "security.tls_strict", settings.tls_strict, &report);
    apply(pairs, "shell.session_restore", settings.session_restore, &report);
    apply(pairs, "shell.devtools", settings.devtools, &report);
    apply(pairs, "shell.spellcheck", settings.spellcheck, &report);
    apply(pairs, "shell.spellcheck_languages", settings.spellcheck_languages, &report);
    apply(pairs, "shell.autofill", settings.autofill, &report);
    apply(pairs, "shell.autofill_save", settings.autofill_save, &report);
    apply(pairs, "privacy.gpc", settings.gpc, &report);
    apply(pairs, "privacy.blocklist", settings.blocklist, &report);
    apply(pairs, "privacy.blocklist_file", settings.blocklist_file, &report);
    apply(pairs, "privacy.dns_over_https", settings.dns_over_https, &report);
    settings.proxy_mode = proxy_mode_value(pairs, "proxy.mode", settings.proxy_mode, &report);
    apply(pairs, "proxy.upstream", settings.proxy_upstream, &report);
    apply(pairs, "proxy.user", settings.proxy_user, &report);
    apply(pairs, "proxy.password", settings.proxy_password, &report);

    for (auto it = pairs.begin(); it != pairs.end();) {
        const std::string prefix = "search.engines.";
        if (it->first.rfind(prefix, 0) != 0) {
            ++it;
            continue;
        }
        std::string rest = it->first.substr(prefix.size());
        size_t dot = rest.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == rest.size()) {
            report.invalid_values.push_back(it->first);
            it = pairs.erase(it);
            continue;
        }
        std::string keyword = rest.substr(0, dot);
        std::string field = rest.substr(dot + 1);
        SearchEngine* engine = nullptr;
        for (auto& candidate : settings.search_engines) {
            if (candidate.keyword == keyword) engine = &candidate;
        }
        if (!engine) {
            SearchEngine fresh;
            fresh.keyword = keyword;
            settings.search_engines.push_back(std::move(fresh));
            engine = &settings.search_engines.back();
        }
        if (field == "name") {
            engine->name = it->second;
        } else if (field == "template") {
            engine->url_template = it->second;
        } else {
            report.invalid_values.push_back(it->first);
        }
        ++report.applied;
        it = pairs.erase(it);
    }

    for (const auto& [key, value] : pairs) {
        report.unknown_keys.push_back(key);
    }
    return settings;
}

void save_settings(const std::string& path, const Settings& settings) {
    std::ofstream out(path);
    if (!out) return;

    out << "version = " << kSettingsVersion << "\n";
    out << "\n[shell]\n";
    out << "homepage = \"" << settings.homepage << "\"\n";
    out << "search_template = \"" << settings.search_template << "\"\n";
    out << "window_width = " << settings.window_width << "\n";
    out << "window_height = " << settings.window_height << "\n";
    out << "zoom = " << clamp_zoom(settings.zoom_default) << "\n";
    out << "session_restore = " << (settings.session_restore ? "true" : "false") << "\n";
    out << "devtools = " << (settings.devtools ? "true" : "false") << "\n";
    out << "spellcheck = " << (settings.spellcheck ? "true" : "false") << "\n";
    out << "spellcheck_languages = \"" << settings.spellcheck_languages << "\"\n";
    out << "autofill = " << (settings.autofill ? "true" : "false") << "\n";
    out << "autofill_save = " << (settings.autofill_save ? "true" : "false") << "\n";
    out << "profile = \"" << settings.shell_profile << "\"\n";
    out << "\n[search]\n";
    out << "default = \"" << settings.search_default << "\"\n";
    if (!settings.search_engines.empty()) {
        out << "\n[search.engines]\n";
        for (const auto& engine : settings.search_engines) {
            out << engine.keyword << ".name = \"" << engine.name << "\"\n";
            out << engine.keyword << ".template = \"" << engine.url_template << "\"\n";
        }
    }
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
    out << "gpc = " << (settings.gpc ? "true" : "false") << "\n";
    out << "blocklist = " << (settings.blocklist ? "true" : "false") << "\n";
    out << "blocklist_file = \"" << settings.blocklist_file << "\"\n";
    out << "dns_over_https = \"" << settings.dns_over_https << "\"\n";
    out << "\n[perf]\n";
    out << "process_model = \"" << settings.process_model << "\"\n";
    out << "startup_budget_ms = " << settings.startup_budget_ms << "\n";
    out << "memory_ceiling_mb = " << settings.memory_ceiling_mb << "\n";
    out << "memory_action = \"" << settings.memory_action << "\"\n";
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
    out << "\n[proxy]\n";
    out << "mode = \"" << settings.proxy_mode << "\"\n";
    out << "upstream = \"" << settings.proxy_upstream << "\"\n";
    out << "user = \"" << settings.proxy_user << "\"\n";
    out << "password = \"" << settings.proxy_password << "\"\n";
}

}
