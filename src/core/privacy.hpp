#pragma once

namespace mustela {

struct Settings;

enum class TabPrivacy {
    Normal,
    Private
};

struct PrivacyPolicy {
    bool private_tabs_available = true;
    bool history_enabled = true;
    TabPrivacy default_private = TabPrivacy::Normal;

    TabPrivacy tab_privacy(TabPrivacy requested) const {
        if (requested == TabPrivacy::Private && private_tabs_available) return TabPrivacy::Private;
        return TabPrivacy::Normal;
    }

    bool records_history(TabPrivacy privacy) const {
        return history_enabled && privacy == TabPrivacy::Normal;
    }
};

PrivacyPolicy privacy_from(const Settings& settings);

}
