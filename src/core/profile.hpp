#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "browsing_data.hpp"
#include "privacy.hpp"

namespace mustela {

enum class ProfileStore : std::uint8_t {
    History,
    Bookmarks,
    Session,
    PerfLog,
    Filters,
    Favicons,
    Autofill
};

struct ProfilePaths {
    std::string root;
    std::string history;
    std::string bookmarks;
    std::string session;
    std::string perf_log;
    std::string filters;
    std::string favicons;
    std::string autofill;
};

class Profile {
public:
    Profile() = default;
    explicit Profile(std::string root);
    Profile(std::string root, const PrivacyPolicy& policy);

    const std::string& root() const { return paths_.root; }
    const ProfilePaths& paths() const { return paths_; }

    const PrivacyPolicy& policy() const { return policy_; }
    void set_policy(const PrivacyPolicy& policy) { policy_ = policy; }

    void ensure_dirs() const;
    bool has(ProfileStore store) const;
    void clear(ProfileStore store) const;

private:
    ProfilePaths paths_;
    PrivacyPolicy policy_;
};

std::string default_profile_root();

struct ProfileEntry {
    std::string id;
    std::string name;
    std::string root;
};

struct ProfileRegistry {
    std::vector<ProfileEntry> entries;
    std::string default_id;

    const ProfileEntry* find(const std::string& id) const;
    void upsert(ProfileEntry entry);
    bool remove(const std::string& id);
};

ProfileRegistry load_profile_registry(const std::string& path);
void save_profile_registry(const std::string& path, const ProfileRegistry& registry);
ProfileRegistry default_profile_registry();
std::string resolve_profile_root(const ProfileEntry& entry, const std::string& base);

}
