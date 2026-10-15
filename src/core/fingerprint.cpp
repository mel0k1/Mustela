#include "fingerprint.hpp"

#include "fingerprint_scripts.hpp"

namespace mustela {

const char* referrer_trim_name(ReferrerTrim trim) {
    switch (trim) {
        case ReferrerTrim::SameOrigin: return "same-origin";
        case ReferrerTrim::Never: return "no-referrer";
        case ReferrerTrim::Off: break;
    }
    return "off";
}

ReferrerTrim referrer_trim_from(const std::string& name) {
    if (name == "same-origin") return ReferrerTrim::SameOrigin;
    if (name == "no-referrer") return ReferrerTrim::Never;
    return ReferrerTrim::Off;
}

FingerprintPolicy fingerprint_strict() {
    FingerprintPolicy policy;
    policy.block_webgl = true;
    policy.block_canvas_readback = true;
    policy.block_remote_fonts = true;
    policy.referrer = ReferrerTrim::Never;
    return policy;
}

FingerprintPolicy fingerprint_preset(const std::string& name) {
    if (name == "standard") {
        FingerprintPolicy policy;
        policy.referrer = ReferrerTrim::SameOrigin;
        return policy;
    }
    if (name == "strict") return fingerprint_strict();
    return FingerprintPolicy{};
}

std::string fingerprint_referrer_script(ReferrerTrim trim) {
    std::string script = fp_scripts::REFERRER;
    size_t slot = script.find("__POLICY__");
    if (slot != std::string::npos) script.replace(slot, 10, referrer_trim_name(trim));
    return script;
}

}
