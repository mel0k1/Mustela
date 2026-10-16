#pragma once

#include <string>

namespace mustela {

std::string branded_error_page(const std::string& heading, const std::string& detail, const std::string& url);

}
