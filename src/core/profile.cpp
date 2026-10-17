#include "profile.hpp"

#include <cstdlib>
#include <filesystem>
#include <utility>

namespace mustela {

namespace {

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

}
