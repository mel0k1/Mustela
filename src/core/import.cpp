#include "import.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace mustela {

namespace {

std::string read_file_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data;
}

std::string file_name(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string trim(const std::string& in) {
    size_t start = 0;
    size_t end = in.size();
    while (start < end && std::isspace(static_cast<unsigned char>(in[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) --end;
    return in.substr(start, end - start);
}

bool is_dir(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec);
}

bool is_file(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(path), ec);
}

std::vector<std::string> subdirectory_names(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec) || ec) continue;
        names.push_back(it->path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

struct JNode;
using JNodePtr = std::unique_ptr<JNode>;

struct JNode {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JNodePtr> items;
    std::vector<std::pair<std::string, JNodePtr>> members;

    const JNode* member(const char* name) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& entry : members) {
            if (entry.first == name) return entry.second.get();
        }
        return nullptr;
    }

    const JNode* at(size_t index) const {
        if (kind != Kind::Array || index >= items.size()) return nullptr;
        return items[index].get();
    }

    std::string as_string() const {
        return kind == Kind::String ? text : std::string();
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& source) : src(source) {}

    JNodePtr parse() {
        pos = 0;
        depth = 0;
        return parse_value();
    }

private:
    const std::string& src;
    size_t pos = 0;
    int depth = 0;

    bool done() const { return pos >= src.size(); }
    char peek() const { return done() ? '\0' : src[pos]; }

    void skip_space() {
        while (!done()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }

    bool consume(char c) {
        skip_space();
        if (peek() == c) {
            ++pos;
            return true;
        }
        return false;
    }

    void append_utf8(std::string& out, unsigned int code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    unsigned int hex4(bool& ok) {
        unsigned int code = 0;
        ok = true;
        for (int i = 0; i < 4; ++i) {
            if (done()) {
                ok = false;
                return 0;
            }
            char h = src[pos++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned int>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned int>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned int>(h - 'A' + 10);
            else {
                ok = false;
                return 0;
            }
        }
        return code;
    }

    JNodePtr fail() { return nullptr; }

    JNodePtr parse_value() {
        if (depth > 64) return fail();
        skip_space();
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string_node();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        return fail();
    }

    JNodePtr parse_object() {
        if (!consume('{')) return fail();
        ++depth;
        JNodePtr node(new JNode());
        node->kind = JNode::Kind::Object;
        if (consume('}')) {
            --depth;
            return node;
        }
        for (;;) {
            skip_space();
            if (peek() != '"') return fail();
            JNodePtr key = parse_string_node();
            if (!key) return fail();
            if (!consume(':')) return fail();
            JNodePtr value = parse_value();
            if (!value) return fail();
            node->members.emplace_back(key->text, std::move(value));
            if (consume(',')) continue;
            if (consume('}')) break;
            return fail();
        }
        --depth;
        return node;
    }

    JNodePtr parse_array() {
        if (!consume('[')) return fail();
        ++depth;
        JNodePtr node(new JNode());
        node->kind = JNode::Kind::Array;
        if (consume(']')) {
            --depth;
            return node;
        }
        for (;;) {
            JNodePtr value = parse_value();
            if (!value) return fail();
            node->items.push_back(std::move(value));
            if (consume(',')) continue;
            if (consume(']')) break;
            return fail();
        }
        --depth;
        return node;
    }

    JNodePtr parse_string_node() {
        JNodePtr node(new JNode());
        node->kind = JNode::Kind::String;
        if (!parse_raw_string(node->text)) return fail();
        return node;
    }

    bool parse_raw_string(std::string& out) {
        if (!consume('"')) return false;
        while (!done()) {
            char c = src[pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (done()) return false;
            char esc = src[pos++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    bool ok = false;
                    unsigned int code = hex4(ok);
                    if (!ok) return false;
                    if (code >= 0xD800 && code <= 0xDBFF && pos + 6 <= src.size()
                        && src[pos] == '\\' && src[pos + 1] == 'u') {
                        size_t mark = pos;
                        pos += 2;
                        bool low_ok = false;
                        unsigned int low = hex4(low_ok);
                        if (low_ok && low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos = mark;
                            append_utf8(out, 0xFFFD);
                        }
                    }
                    append_utf8(out, code);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    JNodePtr parse_bool() {
        JNodePtr node(new JNode());
        if (src.compare(pos, 4, "true") == 0) {
            node->kind = JNode::Kind::Bool;
            node->boolean = true;
            pos += 4;
            return node;
        }
        if (src.compare(pos, 5, "false") == 0) {
            node->kind = JNode::Kind::Bool;
            node->boolean = false;
            pos += 5;
            return node;
        }
        return fail();
    }

    JNodePtr parse_null() {
        if (src.compare(pos, 4, "null") != 0) return fail();
        pos += 4;
        JNodePtr node(new JNode());
        node->kind = JNode::Kind::Null;
        return node;
    }

    JNodePtr parse_number() {
        size_t start = pos;
        if (peek() == '-') ++pos;
        while (!done() && ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.'
            || src[pos] == 'e' || src[pos] == 'E' || src[pos] == '+' || src[pos] == '-')) {
            ++pos;
        }
        if (pos == start) return fail();
        JNodePtr node(new JNode());
        node->kind = JNode::Kind::Number;
        node->number = std::strtod(src.substr(start, pos - start).c_str(), nullptr);
        return node;
    }
};

bool decode_lz4_block(const unsigned char* data, size_t size, size_t expected, std::string& out) {
    out.clear();
    out.reserve(expected);
    size_t i = 0;
    while (i < size) {
        unsigned char token = data[i++];
        size_t literal = token >> 4;
        if (literal == 15) {
            unsigned char extra = 0;
            do {
                if (i >= size) return false;
                extra = data[i++];
                literal += extra;
            } while (extra == 255);
        }
        if (literal > size - i) return false;
        out.append(reinterpret_cast<const char*>(data + i), literal);
        i += literal;
        if (i >= size) break;
        if (out.size() > expected) return false;
        if (i + 2 > size) return false;
        size_t offset = static_cast<size_t>(data[i]) | (static_cast<size_t>(data[i + 1]) << 8);
        i += 2;
        if (offset == 0 || offset > out.size()) return false;
        size_t match = static_cast<size_t>(token & 0x0F) + 4;
        unsigned char extra = 0;
        if ((token & 0x0F) == 15) {
            do {
                if (i >= size) return false;
                extra = data[i++];
                match += extra;
            } while (extra == 255);
        }
        if (out.size() + match > expected) return false;
        size_t from = out.size() - offset;
        for (size_t n = 0; n < match; ++n) {
            out += out[from + n];
        }
    }
    return out.size() == expected;
}

std::int64_t node_int(const JNode* node, std::int64_t fallback) {
    if (!node) return fallback;
    if (node->kind == JNode::Kind::Number) return static_cast<std::int64_t>(node->number);
    if (node->kind == JNode::Kind::String) {
        char* end = nullptr;
        std::int64_t parsed = std::strtoll(node->text.c_str(), &end, 10);
        return end && *end == '\0' && !node->text.empty() ? parsed : fallback;
    }
    return fallback;
}

void add_bookmark(BookmarkStore& into, const std::string& url, const std::string& title,
    std::int64_t added_at, ImportReport& report) {
    if (url.empty()) return;
    if (into.contains(url)) {
        ++report.bookmarks_skipped;
        return;
    }
    if (into.add(url, title, added_at)) ++report.bookmarks_added;
}

void walk_chromium_node(const JNode* node, BookmarkStore& into, ImportReport& report) {
    if (!node) return;
    if (node->kind == JNode::Kind::Array) {
        for (const JNodePtr& item : node->items) walk_chromium_node(item.get(), into, report);
        return;
    }
    if (node->kind != JNode::Kind::Object) return;
    const JNode* type = node->member("type");
    if (!type || type->kind != JNode::Kind::String) return;
    if (type->text == "url") {
        const JNode* url = node->member("url");
        if (!url || url->kind != JNode::Kind::String) return;
        std::int64_t when = chrome_time_to_unix(node_int(node->member("date_added"), 0));
        const JNode* name = node->member("name");
        add_bookmark(into, url->text, name ? name->as_string() : std::string(), when, report);
        return;
    }
    if (type->text == "folder") {
        const JNode* children = node->member("children");
        if (children) walk_chromium_node(children, into, report);
    }
}

void walk_firefox_node(const JNode* node, BookmarkStore& into, ImportReport& report) {
    if (!node) return;
    if (node->kind == JNode::Kind::Array) {
        for (const JNodePtr& item : node->items) walk_firefox_node(item.get(), into, report);
        return;
    }
    if (node->kind != JNode::Kind::Object) return;
    std::int64_t type = node_int(node->member("typeCode"), 0);
    if (type == 1) {
        const JNode* uri = node->member("uri");
        if (!uri || uri->kind != JNode::Kind::String) return;
        std::int64_t when = firefox_time_to_unix(node_int(node->member("dateAdded"), 0));
        const JNode* title = node->member("title");
        add_bookmark(into, uri->text, title ? title->as_string() : std::string(), when, report);
        return;
    }
    if (type == 2 || type == 3) {
        const JNode* children = node->member("children");
        if (children) walk_firefox_node(children, into, report);
    }
}

std::string html_entity_decode(const std::string& in) {
    static const std::pair<const char*, const char*> table[] = {
        {"amp;", "&"}, {"lt;", "<"}, {"gt;", ">"}, {"quot;", "\""},
        {"apos;", "'"}, {"nbsp;", " "}, {"#39;", "'"}, {"#47;", "/"}};
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in[i] != '&') {
            out += in[i++];
            continue;
        }
        bool replaced = false;
        for (const auto& entry : table) {
            size_t len = std::strlen(entry.first);
            if (in.compare(i + 1, len, entry.first) == 0) {
                out += entry.second;
                i += 1 + len;
                replaced = true;
                break;
            }
        }
        if (!replaced) out += in[i++];
    }
    return out;
}

std::string attr_value(const std::string& tag_lower, const std::string& tag_raw, const char* name) {
    std::string needle(name);
    needle += '=';
    size_t pos = tag_lower.find(needle);
    if (pos == std::string::npos) return std::string();
    pos += needle.size();
    if (pos >= tag_raw.size()) return std::string();
    char quote = tag_raw[pos];
    if (quote != '"' && quote != '\'') {
        size_t end = tag_lower.find_first_of(" >", pos);
        return html_entity_decode(tag_raw.substr(pos, end == std::string::npos
            ? std::string::npos : end - pos));
    }
    size_t start = pos + 1;
    size_t end = tag_raw.find(quote, start);
    if (end == std::string::npos) return std::string();
    return html_entity_decode(tag_raw.substr(start, end - start));
}

std::string to_lower(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string newest_jsonlz4(const std::string& backup_dir) {
    std::string newest;
    bool have = false;
    fs::file_time_type newest_time;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(backup_dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (name.size() < 8 || name.compare(name.size() - 8, 8, ".jsonlz4") != 0) continue;
        fs::file_time_type when = fs::last_write_time(it->path(), ec);
        if (ec) continue;
        if (!have || when > newest_time) {
            have = true;
            newest_time = when;
            newest = it->path().string();
        }
    }
    return newest;
}

}

std::int64_t chrome_time_to_unix(std::int64_t value) {
    if (value <= 0) return 0;
    const std::int64_t chrome_epoch_delta = 11644473600;
    std::int64_t seconds = value / 1000000;
    if (seconds < chrome_epoch_delta) return 0;
    return seconds - chrome_epoch_delta;
}

std::int64_t firefox_time_to_unix(std::int64_t value) {
    if (value <= 0) return 0;
    if (value > 100000000000) return value / 1000000;
    return value;
}

std::vector<DetectedProfile> detect_browsers() {
    std::vector<DetectedProfile> found;
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    if (!local || !*local) return found;
    std::string base = local;
    auto scan_chromium_windows = [&](const char* browser, const std::string& dir) {
        if (!is_dir(dir)) return;
        for (const std::string& name : subdirectory_names(dir)) {
            std::string profile_dir = dir + "\\" + name;
            if (!is_file(profile_dir + "\\Bookmarks")) continue;
            DetectedProfile item;
            item.browser = browser;
            item.profile = name;
            item.path = profile_dir;
            item.has_bookmarks = true;
            found.push_back(std::move(item));
        }
    };
    scan_chromium_windows("Chrome", base + "\\Google\\Chrome\\User Data");
    scan_chromium_windows("Edge", base + "\\Microsoft\\Edge\\User Data");
    scan_chromium_windows("Brave", base + "\\BraveSoftware\\Brave-Browser");
    scan_chromium_windows("Vivaldi", base + "\\Vivaldi\\User Data");
    scan_chromium_windows("Chromium", base + "\\Chromium\\User Data");
    scan_chromium_windows("Opera", base + "\\Opera Software\\Opera Stable");
    return found;
#else
    const char* home = std::getenv("HOME");
    return detect_browsers_in(home && *home ? std::string(home) : std::string("."));
#endif
}

std::vector<DetectedProfile> detect_browsers_in(const std::string& home) {
    std::vector<DetectedProfile> found;
    auto scan_chromium = [&](const char* browser, const std::string& dir) {
        if (!is_dir(dir)) return;
        for (const std::string& name : subdirectory_names(dir)) {
            std::string profile_dir = dir + "/" + name;
            if (!is_file(profile_dir + "/Bookmarks")) continue;
            DetectedProfile item;
            item.browser = browser;
            item.profile = name;
            item.path = profile_dir;
            item.has_bookmarks = true;
            found.push_back(std::move(item));
        }
    };
    scan_chromium("Chrome", home + "/.config/google-chrome");
    scan_chromium("Chromium", home + "/.config/chromium");
    scan_chromium("Edge", home + "/.config/microsoft-edge");
    scan_chromium("Brave", home + "/.config/BraveSoftware/Brave-Browser");
    scan_chromium("Vivaldi", home + "/.config/vivaldi");
    scan_chromium("Opera", home + "/.config/opera");
    std::string firefox_root = home + "/.mozilla/firefox";
    if (is_dir(firefox_root)) {
        for (const std::string& name : subdirectory_names(firefox_root)) {
            std::string profile_dir = firefox_root + "/" + name;
            bool has = is_dir(profile_dir + "/bookmarkbackups") || is_file(profile_dir + "/bookmarks.html");
            if (!has) continue;
            DetectedProfile item;
            item.browser = "Firefox";
            item.profile = name;
            item.path = profile_dir;
            item.has_bookmarks = true;
            found.push_back(std::move(item));
        }
    }
    return found;
}

ImportReport import_bookmarks_chromium(const std::string& profile_dir, BookmarkStore& into) {
    ImportReport report;
    std::string data = read_file_bytes(profile_dir + "/Bookmarks");
    if (data.empty()) {
        report.error = "Bookmarks file not found in " + profile_dir;
        return report;
    }
    JsonParser parser(data);
    JNodePtr root = parser.parse();
    if (!root || root->kind != JNode::Kind::Object) {
        report.error = "Bookmarks file is not valid JSON";
        return report;
    }
    const JNode* roots = root->member("roots");
    if (!roots || roots->kind != JNode::Kind::Object) {
        report.error = "Bookmarks file has no roots";
        return report;
    }
    for (const auto& entry : roots->members) {
        walk_chromium_node(entry.second.get(), into, report);
    }
    return report;
}

ImportReport import_bookmarks_firefox(const std::string& profile_dir, BookmarkStore& into) {
    ImportReport report;
    std::string backup_dir = profile_dir + "/bookmarkbackups";
    std::string newest;
    if (is_dir(backup_dir)) newest = newest_jsonlz4(backup_dir);
    if (newest.empty()) {
        std::string legacy = profile_dir + "/bookmarks.html";
        if (is_file(legacy)) return import_bookmarks_html(legacy, into);
        report.error = "no Firefox bookmark backups found (bookmarkbackups/*.jsonlz4)";
        return report;
    }
    std::string blob = read_file_bytes(newest);
    static const char moz_magic[8] = {'m', 'o', 'z', 'L', 'z', '4', '0', '\0'};
    if (blob.size() < 12 || std::memcmp(blob.data(), moz_magic, 8) != 0) {
        report.error = "invalid Firefox backup format (" + file_name(newest) + ")";
        return report;
    }
    size_t expected = 0;
    for (int i = 0; i < 4; ++i) {
        expected |= static_cast<size_t>(static_cast<unsigned char>(blob[8 + i])) << (8 * i);
    }
    if (expected == 0 || expected > 256 * 1024 * 1024) {
        report.error = "Firefox backup has an unreasonable size";
        return report;
    }
    std::string json;
    if (!decode_lz4_block(reinterpret_cast<const unsigned char*>(blob.data()) + 12,
        blob.size() - 12, expected, json)) {
        report.error = "Firefox backup is corrupted";
        return report;
    }
    JsonParser parser(json);
    JNodePtr root = parser.parse();
    if (!root || root->kind != JNode::Kind::Object) {
        report.error = "Firefox backup JSON is not valid";
        return report;
    }
    const JNode* roots = root->member("roots");
    if (roots && roots->kind == JNode::Kind::Object) {
        for (const auto& entry : roots->members) {
            walk_firefox_node(entry.second.get(), into, report);
        }
    }
    return report;
}

ImportReport import_bookmarks_html(const std::string& html_path, BookmarkStore& into) {
    ImportReport report;
    std::string data = read_file_bytes(html_path);
    if (data.empty()) {
        report.error = "cannot read " + html_path;
        return report;
    }
    std::string lower = to_lower(data);
    size_t pos = 0;
    for (;;) {
        size_t tag = lower.find("<a", pos);
        if (tag == std::string::npos) break;
        size_t tag_end = lower.find('>', tag);
        if (tag_end == std::string::npos) break;
        std::string tag_lower = lower.substr(tag, tag_end - tag + 1);
        std::string tag_raw = data.substr(tag, tag_end - tag + 1);
        size_t close = lower.find("</a>", tag_end);
        std::string title = close == std::string::npos
            ? std::string()
            : trim(html_entity_decode(data.substr(tag_end + 1, close - tag_end - 1)));
        pos = close == std::string::npos ? tag_end : close + 4;
        std::string url = attr_value(tag_lower, tag_raw, "href");
        if (url.empty()) continue;
        std::string date = attr_value(tag_lower, tag_raw, "add_date");
        std::int64_t when = date.empty() ? 0 : std::strtoll(date.c_str(), nullptr, 10);
        add_bookmark(into, url, title, when, report);
    }
    return report;
}

}
