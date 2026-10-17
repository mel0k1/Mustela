#pragma once

#include <cstdint>
#include <string>

#include "browsing_data.hpp"
#include "privacy.hpp"

namespace mustela {

enum class ProfileStore : std::uint8_t {
    History,
    Bookmarks,
    Session,
    PerfLog,
    Filters,
    Favicons
};

struct ProfilePaths {
    std::string root;
    std::string history;
    std::string bookmarks;
    std::string session;
    std::string perf_log;
    std::string filters;
    std::string favicons;
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

}
