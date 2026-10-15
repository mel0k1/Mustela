#include "history.hpp"

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

HistoryStore::HistoryStore(std::string path, std::size_t capacity)
    : path_(std::move(path)), capacity_(capacity) {
    load();
}

HistoryStore::~HistoryStore() {
    save();
}

void HistoryStore::load() {
    std::ifstream in(path_);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        size_t first = line.find('\t');
        if (first == std::string::npos) continue;
        size_t second = line.find('\t', first + 1);
        if (second == std::string::npos) continue;

        HistoryEntry entry;
        entry.url = unescape_field(line.substr(0, first));
        entry.title = unescape_field(line.substr(first + 1, second - first - 1));
        entry.visited_at = std::strtoll(line.substr(second + 1).c_str(), nullptr, 10);
        if (!entry.url.empty()) entries_.push_back(std::move(entry));
    }
}

void HistoryStore::save() const {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    for (const HistoryEntry& entry : entries_) {
        out << escape_field(entry.url) << '\t'
            << escape_field(entry.title) << '\t'
            << entry.visited_at << '\n';
    }
}

void HistoryStore::record(const std::string& url, const std::string& title, std::int64_t visited_at) {
    if (url.empty()) return;

    if (!entries_.empty() && entries_.back().url == url) {
        entries_.back().title = title;
        entries_.back().visited_at = visited_at;
        return;
    }

    HistoryEntry entry;
    entry.url = url;
    entry.title = title;
    entry.visited_at = visited_at;
    entries_.push_back(std::move(entry));

    if (entries_.size() > capacity_) {
        entries_.erase(entries_.begin(), entries_.begin() + (entries_.size() - capacity_));
    }
}

std::vector<HistoryEntry> HistoryStore::recent(std::size_t limit) const {
    std::vector<HistoryEntry> out;
    size_t count = std::min(limit, entries_.size());
    out.reserve(count);
    for (size_t i = entries_.size(); i > entries_.size() - count; --i) {
        out.push_back(entries_[i - 1]);
    }
    return out;
}

void HistoryStore::clear() {
    entries_.clear();
    save();
}

}
