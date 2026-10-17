#pragma once

#include <string>

namespace mustela {

bool is_public_suffix(const std::string& host);
std::string registrable_domain(const std::string& host);
std::string psl_version();

}
