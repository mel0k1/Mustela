#pragma once

#include <string>

#include "site_rules.hpp"

namespace mustela {

enum class PermissionChoice {
    Deny,
    Ask,
    Allow
};

enum class PermissionDecision {
    Denied,
    Prompt,
    Granted
};

PermissionChoice permission_choice_from(const std::string& name);
const char* permission_choice_name(PermissionChoice choice);
PermissionDecision resolve_permission(PermissionChoice global, SiteRule site);

}
