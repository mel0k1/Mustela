#pragma once

#include <string>

namespace mustela {

struct Settings;

enum class TabPrivacy {
    Normal,
    Private
};

enum class CookiePolicy {
    Always,
    NoThirdParty,
    Never
};

struct Hardening {
    bool tracking_prevention = true;
    bool javascript = true;
    bool hyperlink_auditing = false;
    bool dns_prefetching = false;
    bool autoplay_blocked = true;
};

struct PrivacyPolicy {
    bool private_tabs_available = true;
    bool history_enabled = true;
    TabPrivacy default_private = TabPrivacy::Normal;
    CookiePolicy cookies = CookiePolicy::NoThirdParty;
    Hardening hardening;

    TabPrivacy tab_privacy(TabPrivacy requested) const {
        if (requested == TabPrivacy::Private && private_tabs_available) return TabPrivacy::Private;
        return TabPrivacy::Normal;
    }

    bool records_history(TabPrivacy privacy) const {
        return history_enabled && privacy == TabPrivacy::Normal;
    }
};

CookiePolicy cookie_policy_from(const std::string& name);
const char* cookie_policy_name(CookiePolicy policy);
PrivacyPolicy privacy_from(const Settings& settings);

}
