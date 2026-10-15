#include "url.hpp"

namespace mustela {

namespace {

std::string trim_copy(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
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

}

std::string normalize_url(const std::string& input, const std::string& search_template) {
    std::string text = trim_copy(input);
    if (text.empty()) return "";
    if (text.find("://") != std::string::npos) return text;

    bool host_like = text.rfind("localhost", 0) == 0 ||
                     (text.find('.') != std::string::npos && text.find(' ') == std::string::npos);
    if (host_like) return "https://" + text;

    size_t slot = search_template.find("%s");
    if (slot == std::string::npos) return search_template;
    return search_template.substr(0, slot) + percent_encode(text) + search_template.substr(slot + 2);
}

}
