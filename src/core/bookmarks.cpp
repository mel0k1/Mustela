#include "bookmarks.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace mustela {

namespace {

std::string escape_field(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string unescape_field(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char next = text[++i];
            switch (next) {
                case 't': out += '\t'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                default: out += next; break;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}

}

BookmarkStore::BookmarkStore(std::string path)
    : path_(std::move(path)) {
    load();
}

BookmarkStore::~BookmarkStore() {
    save();
}

void BookmarkStore::load() {
    std::ifstream in(path_);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        size_t first = line.find('\t');
        if (first == std::string::npos) continue;
        size_t second = line.find('\t', first + 1);
        if (second == std::string::npos) continue;

        BookmarkEntry entry;
        entry.url = unescape_field(line.substr(0, first));
        entry.title = unescape_field(line.substr(first + 1, second - first - 1));
        entry.added_at = std::strtoll(line.substr(second + 1).c_str(), nullptr, 10);
        if (!entry.url.empty()) entries_.push_back(std::move(entry));
    }
}

void BookmarkStore::save() const {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    for (const BookmarkEntry& entry : entries_) {
        out << escape_field(entry.url) << '\t'
            << escape_field(entry.title) << '\t'
            << entry.added_at << '\n';
    }
}

bool BookmarkStore::add(const std::string& url, const std::string& title, std::int64_t added_at) {
    if (url.empty()) return false;
    for (BookmarkEntry& entry : entries_) {
        if (entry.url == url) {
            entry.title = title;
            entry.added_at = added_at;
            save();
            return true;
        }
    }
    BookmarkEntry entry;
    entry.url = url;
    entry.title = title;
    entry.added_at = added_at;
    entries_.push_back(std::move(entry));
    save();
    return true;
}

bool BookmarkStore::remove(const std::string& url) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->url == url) {
            entries_.erase(it);
            save();
            return true;
        }
    }
    return false;
}

bool BookmarkStore::contains(const std::string& url) const {
    return std::any_of(entries_.begin(), entries_.end(),
        [&url](const BookmarkEntry& entry) { return entry.url == url; });
}

}
