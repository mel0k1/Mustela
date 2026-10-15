#pragma once

#include <string>

namespace mustela {

enum class ReferrerTrim {
    Off,
    SameOrigin,
    Never
};

struct FingerprintPolicy {
    bool block_webgl = false;
    bool block_canvas_readback = false;
    bool block_remote_fonts = false;
    ReferrerTrim referrer = ReferrerTrim::Off;

    bool active() const {
        return block_webgl || block_canvas_readback || block_remote_fonts || referrer != ReferrerTrim::Off;
    }
};

const char* referrer_trim_name(ReferrerTrim trim);
ReferrerTrim referrer_trim_from(const std::string& name);
FingerprintPolicy fingerprint_strict();
FingerprintPolicy fingerprint_preset(const std::string& name);
std::string fingerprint_referrer_script(ReferrerTrim trim);

}
