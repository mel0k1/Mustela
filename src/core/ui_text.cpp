#include "ui_text.hpp"

namespace mustela {

namespace {

size_t utf8_floor(const std::string& text, size_t limit) {
    if (limit >= text.size()) return text.size();
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return cut;
}

bool is_dropped_control(unsigned char c) {
    return c <= 0x08 || c == 0x0B || c == 0x0C || (c >= 0x0E && c <= 0x1F) || c == 0x7F;
}

}

std::string sanitize_dialog_text(const std::string& text, size_t max_bytes) {
    std::string clean;
    clean.reserve(text.size() < max_bytes ? text.size() : max_bytes);
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            clean += '\n';
            continue;
        }
        if (is_dropped_control(c)) continue;
        clean += static_cast<char>(c);
    }
    if (clean.size() > max_bytes) {
        size_t cut = utf8_floor(clean, max_bytes > 3 ? max_bytes - 3 : 0);
        clean.resize(cut);
        clean += "\xE2\x80\xA6";
    }
    return clean;
}

std::string sanitize_find_query(const std::string& query, size_t max_bytes) {
    std::string clean;
    clean.reserve(query.size() < max_bytes ? query.size() : max_bytes);
    for (size_t i = 0; i < query.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(query[i]);
        if (c == '\r') {
            if (i + 1 < query.size() && query[i + 1] == '\n') continue;
            clean += ' ';
            continue;
        }
        if (c == '\n') {
            clean += ' ';
            continue;
        }
        if (is_dropped_control(c)) continue;
        clean += static_cast<char>(c);
    }
    if (clean.size() > max_bytes) {
        clean.resize(utf8_floor(clean, max_bytes));
    }
    return clean;
}

std::string sanitize_filename(const std::string& name, size_t max_bytes) {
    std::string clean;
    clean.reserve(name.size() < max_bytes ? name.size() : max_bytes);
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (is_dropped_control(c) || c == '/' || c == '\\') continue;
        clean += static_cast<char>(c);
    }
    size_t begin = clean.find_first_not_of(". ");
    if (begin == std::string::npos) return "download";
    size_t end = clean.find_last_not_of(". ");
    clean = clean.substr(begin, end - begin + 1);
    if (clean.empty()) return "download";
    if (clean.size() > max_bytes) {
        clean.resize(utf8_floor(clean, max_bytes));
        while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) clean.pop_back();
        if (clean.empty()) return "download";
    }
    return clean;
}

}
