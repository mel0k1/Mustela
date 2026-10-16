#pragma once

#include <cstddef>
#include <string>

namespace mustela {

std::string sanitize_dialog_text(const std::string& text, size_t max_bytes = 4096);
std::string sanitize_find_query(const std::string& query, size_t max_bytes = 256);
std::string sanitize_filename(const std::string& name, size_t max_bytes = 128);

}
