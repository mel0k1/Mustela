#include "profile.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace mustela {

namespace {

std::string trim_copy(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool is_dir_store(ProfileStore store) {
    return store == ProfileStore::Filters || store == ProfileStore::Favicons;
}

const std::string& store_path(const ProfilePaths& paths, ProfileStore store) {
    switch (store) {
        case ProfileStore::History: return paths.history;
        case ProfileStore::Bookmarks: return paths.bookmarks;
        case ProfileStore::Session: return paths.session;
        case ProfileStore::PerfLog: return paths.perf_log;
        case ProfileStore::Filters: return paths.filters;
        case ProfileStore::Favicons: return paths.favicons;
        case ProfileStore::Autofill: return paths.autofill;
    }
    return paths.root;
}

}

Profile::Profile(std::string root) {
    paths_.root = std::move(root);
    paths_.history = paths_.root + "/history.dat";
    paths_.bookmarks = paths_.root + "/bookmarks.tsv";
    paths_.session = paths_.root + "/session.json";
    paths_.perf_log = paths_.root + "/perf.jsonl";
    paths_.filters = paths_.root + "/filters";
    paths_.favicons = paths_.root + "/favicons";
    paths_.autofill = paths_.root + "/autofill.tsv";
}

Profile::Profile(std::string root, const PrivacyPolicy& policy) : Profile(std::move(root)) {
    policy_ = policy;
}

void Profile::ensure_dirs() const {
    std::error_code error;
    std::filesystem::create_directories(paths_.root, error);
    std::filesystem::create_directories(paths_.filters, error);
    std::filesystem::create_directories(paths_.favicons, error);
}

bool Profile::has(ProfileStore store) const {
    std::error_code error;
    const std::string& path = store_path(paths_, store);
    if (is_dir_store(store)) return std::filesystem::is_directory(path, error);
    return std::filesystem::exists(path, error);
}

void Profile::clear(ProfileStore store) const {
    std::error_code error;
    const std::string& path = store_path(paths_, store);
    if (is_dir_store(store)) std::filesystem::remove_all(path, error);
    else std::filesystem::remove(path, error);
}

std::string default_profile_root() {
#if defined(_WIN32)
    const char* local = std::getenv("LOCALAPPDATA");
    const char* roaming = std::getenv("APPDATA");
    std::string base = (local && *local) ? local : (roaming ? roaming : "");
    if (base.empty()) return "mustela/profiles/default";
    return base + "/mustela/profiles/default";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = (home && *home ? std::string(home) : std::string(".")) + "/.local/share";
    }
    return base + "/mustela/profiles/default";
#endif
}

const ProfileEntry* ProfileRegistry::find(const std::string& id) const {
    for (const auto& entry : entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

void ProfileRegistry::upsert(ProfileEntry entry) {
    if (entry.id.empty()) return;
    for (auto& existing : entries) {
        if (existing.id == entry.id) {
            existing.name = std::move(entry.name);
            existing.root = std::move(entry.root);
            return;
        }
    }
    entries.push_back(std::move(entry));
}

bool ProfileRegistry::remove(const std::string& id) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->id == id) {
            entries.erase(it);
            if (default_id == id) default_id = entries.empty() ? "" : entries.front().id;
            return true;
        }
    }
    return false;
}

ProfileRegistry default_profile_registry() {
    ProfileRegistry registry;
    ProfileEntry entry;
    entry.id = "default";
    entry.name = "Default";
    entry.root = "profiles/default";
    registry.entries.push_back(std::move(entry));
    registry.default_id = "default";
    return registry;
}

ProfileRegistry load_profile_registry(const std::string& path) {
    ProfileRegistry registry = default_profile_registry();
    std::ifstream in(path);
    if (!in) return registry;

    ProfileRegistry parsed;
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        std::string text = trim_copy(line);
        if (text.empty() || text[0] == '#') continue;
        if (text.front() == '[' && text.back() == ']') {
            section = trim_copy(text.substr(1, text.size() - 2));
            continue;
        }
        size_t split = text.find('=');
        if (split == std::string::npos) continue;
        std::string key = trim_copy(text.substr(0, split));
        std::string value = trim_copy(text.substr(split + 1));
        if (value.empty()) continue;
        if (value.front() == '"') {
            size_t closing = value.find('"', 1);
            if (closing == std::string::npos) continue;
            value = value.substr(1, closing - 1);
        }

        if (section.empty() && key == "default") {
            parsed.default_id = value;
            continue;
        }
        const std::string prefix = "profiles.";
        if (section.rfind(prefix, 0) != 0 && key.rfind(prefix, 0) != 0) continue;
        std::string scoped = section.rfind(prefix, 0) == 0 ? section + "." + key : key;
        std::string rest = scoped.substr(prefix.size());
        size_t dot = rest.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == rest.size()) continue;
        std::string id = rest.substr(0, dot);
        std::string field = rest.substr(dot + 1);
        ProfileEntry* entry = nullptr;
        for (auto& candidate : parsed.entries) {
            if (candidate.id == id) entry = &candidate;
        }
        if (!entry) {
            ProfileEntry fresh;
            fresh.id = id;
            parsed.entries.push_back(std::move(fresh));
            entry = &parsed.entries.back();
        }
        if (field == "name") {
            entry->name = value;
        } else if (field == "root") {
            entry->root = value;
        }
    }

    if (parsed.entries.empty()) return registry;
    if (parsed.default_id.empty() || !parsed.find(parsed.default_id)) {
        parsed.default_id = parsed.entries.front().id;
    }
    return parsed;
}

void save_profile_registry(const std::string& path, const ProfileRegistry& registry) {
    std::ofstream out(path);
    if (!out) return;
    out << "default = \"" << registry.default_id << "\"\n";
    for (const auto& entry : registry.entries) {
        out << "\n[profiles." << entry.id << "]\n";
        out << "name = \"" << entry.name << "\"\n";
        out << "root = \"" << entry.root << "\"\n";
    }
}

std::string resolve_profile_root(const ProfileEntry& entry, const std::string& base) {
    if (entry.root.empty()) return base + "/" + entry.id;
    if (!entry.root.empty() && entry.root.front() == '/') return entry.root;
#if defined(_WIN32)
    if (entry.root.size() >= 2 && entry.root[1] == ':') return entry.root;
#endif
    return base + "/" + entry.root;
}

}
