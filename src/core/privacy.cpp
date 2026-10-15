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
    policy.fingerprint.block_webgl = settings.fingerprint_webgl;
    policy.fingerprint.block_canvas_readback = settings.fingerprint_canvas;
    policy.fingerprint.block_remote_fonts = settings.fingerprint_fonts;
    policy.fingerprint.referrer = referrer_trim_from(settings.fingerprint_referrer);
    return policy;
}

SiteDecision resolve_site(const PrivacyPolicy& policy, const SiteRules& rules, const std::string& uri) {
    SitePolicy site = rules.policy_for(host_from_uri(uri));
    SiteDecision decision;
    decision.javascript = policy.hardening.javascript;
    if (site.javascript == SiteRule::Allow) decision.javascript = true;
    if (site.javascript == SiteRule::Deny) decision.javascript = false;
    decision.cookies_denied = site.cookies == SiteRule::Deny;
    decision.fingerprint = policy.fingerprint;
    if (site.fingerprint == SiteRule::Allow) decision.fingerprint = FingerprintPolicy{};
    if (site.fingerprint == SiteRule::Deny) decision.fingerprint = fingerprint_strict();
    return decision;
}

}
