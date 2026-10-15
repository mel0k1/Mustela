#include "privacy.hpp"

#include "config.hpp"

namespace mustela {

PrivacyPolicy privacy_from(const Settings& settings) {
    PrivacyPolicy policy;
    policy.private_tabs_available = settings.private_tabs_enabled;
    policy.history_enabled = settings.history_enabled;
    policy.default_private = (settings.private_tabs_enabled && settings.start_in_private)
        ? TabPrivacy::Private
        : TabPrivacy::Normal;
    return policy;
}

}
