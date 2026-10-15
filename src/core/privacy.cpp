#include "privacy.hpp"

#include "config.hpp"

namespace mustela {

CookiePolicy cookie_policy_from(const std::string& name) {
    if (name == "always") return CookiePolicy::Always;
    if (name == "never") return CookiePolicy::Never;
    return CookiePolicy::NoThirdParty;
}

const char* cookie_policy_name(CookiePolicy policy) {
    switch (policy) {
        case CookiePolicy::Always: return "always";
        case CookiePolicy::Never: return "never";
        case CookiePolicy::NoThirdParty: return "no-third-party";
    }
    return "no-third-party";
}

PrivacyPolicy privacy_from(const Settings& settings) {
    PrivacyPolicy policy;
    policy.private_tabs_available = settings.private_tabs_enabled;
    policy.history_enabled = settings.history_enabled;
    policy.default_private = (settings.private_tabs_enabled && settings.start_in_private)
        ? TabPrivacy::Private
        : TabPrivacy::Normal;
    policy.cookies = cookie_policy_from(settings.cookie_policy);
    policy.hardening.tracking_prevention = settings.tracking_prevention;
    policy.hardening.javascript = settings.javascript_enabled;
    policy.hardening.hyperlink_auditing = settings.hyperlink_auditing;
    policy.hardening.dns_prefetching = settings.dns_prefetching;
    policy.hardening.autoplay_blocked = settings.block_autoplay;
    return policy;
}

}
