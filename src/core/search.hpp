#pragma once

#include <string>

#include "config.hpp"

namespace mustela {

SearchEngine find_search_engine(const Settings& settings, const std::string& keyword);
SearchEngine default_search_engine(const Settings& settings);
std::string fill_search_template(const SearchEngine& engine, const std::string& query);
std::string resolve_entry(const std::string& text, const Settings& settings);

}
