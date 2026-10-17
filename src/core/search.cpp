#include "search.hpp"

namespace mustela {

namespace {

std::string trim_copy(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string to_lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

std::string percent_encode(const std::string& text) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        if (is_unreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

bool host_like(const std::string& text) {
    return text.rfind("localhost", 0) == 0 ||
           (text.find('.') != std::string::npos && text.find(' ') == std::string::npos);
}

}

SearchEngine find_search_engine(const Settings& settings, const std::string& keyword) {
    SearchEngine missing;
    if (keyword.empty()) return missing;
    std::string wanted = to_lower_ascii(trim_copy(keyword));
    for (const auto& engine : settings.search_engines) {
        if (to_lower_ascii(engine.keyword) == wanted) return engine;
    }
    return missing;
}

SearchEngine default_search_engine(const Settings& settings) {
    if (!settings.search_engines.empty()) {
        if (!settings.search_default.empty()) {
            for (const auto& engine : settings.search_engines) {
                if (to_lower_ascii(engine.keyword) == to_lower_ascii(settings.search_default)) {
                    return engine;
                }
            }
        }
        return settings.search_engines.front();
    }
    SearchEngine fallback;
    fallback.url_template = settings.search_template;
    return fallback;
}

std::string fill_search_template(const SearchEngine& engine, const std::string& query) {
    size_t slot = engine.url_template.find("%s");
    if (slot == std::string::npos) return engine.url_template;
    return engine.url_template.substr(0, slot) + percent_encode(query) +
           engine.url_template.substr(slot + 2);
}

std::string resolve_entry(const std::string& text, const Settings& settings) {
    std::string trimmed = trim_copy(text);
    if (trimmed.empty()) return "";
    if (trimmed.find("://") != std::string::npos) return trimmed;

    if (trimmed[0] == '!') {
        size_t space = trimmed.find_first_of(" \t");
        std::string keyword = space == std::string::npos ? trimmed.substr(1) : trimmed.substr(1, space - 1);
        SearchEngine engine = find_search_engine(settings, keyword);
        if (engine.url_template.empty()) engine = default_search_engine(settings);
        std::string rest = space == std::string::npos ? "" : trim_copy(trimmed.substr(space + 1));
        return fill_search_template(engine, rest);
    }

    if (host_like(trimmed)) return "https://" + trimmed;

    return fill_search_template(default_search_engine(settings), trimmed);
}

}
