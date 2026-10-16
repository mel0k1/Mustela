#include "permissions.hpp"

namespace mustela {

PermissionChoice permission_choice_from(const std::string& name) {
    if (name == "ask") return PermissionChoice::Ask;
    if (name == "allow") return PermissionChoice::Allow;
    return PermissionChoice::Deny;
}

const char* permission_choice_name(PermissionChoice choice) {
    switch (choice) {
        case PermissionChoice::Ask: return "ask";
        case PermissionChoice::Allow: return "allow";
        case PermissionChoice::Deny: return "deny";
    }
    return "deny";
}

PermissionDecision resolve_permission(PermissionChoice global, SiteRule site) {
    if (site == SiteRule::Allow) return PermissionDecision::Granted;
    if (site == SiteRule::Deny) return PermissionDecision::Denied;
    if (global == PermissionChoice::Allow) return PermissionDecision::Granted;
    if (global == PermissionChoice::Ask) return PermissionDecision::Prompt;
    return PermissionDecision::Denied;
}

}
