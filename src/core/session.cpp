#include "session.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace mustela {

namespace {

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool parse(SessionData& out) {
        skip_ws();
        if (!top_object(out)) return false;
        skip_ws();
        return pos_ >= text_.size();
    }

private:
    void skip_ws() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool consume(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool string(std::string& out) {
        if (!consume('"')) return false;
        std::string value;
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                out = std::move(value);
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) return false;
                char escape = text_[pos_];
                switch (escape) {
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    case '/': value.push_back('/'); break;
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case 'b': value.push_back('\b'); break;
                    case 'f': value.push_back('\f'); break;
                    case 'u': {
                        unsigned code = 0;
                        if (!hex4(code)) return false;
                        append_utf8(value, code);
                        break;
                    }
                    default:
                        return false;
                }
                ++pos_;
                continue;
            }
            value.push_back(c);
            ++pos_;
        }
        return false;
    }

    void append_utf8(std::string& value, unsigned code) {
        if (code < 0x80) {
            value.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            value.push_back(static_cast<char>(0xC0 | (code >> 6)));
            value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            value.push_back(static_cast<char>(0xE0 | (code >> 12)));
            value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool hex4(unsigned& out) {
        if (pos_ + 4 > text_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_ + static_cast<size_t>(i)];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else return false;
            out = (out << 4) | digit;
        }
        pos_ += 4;
        return true;
    }

    bool number(int& out) {
        size_t begin = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        size_t digits = 0;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
            ++digits;
        }
        if (digits == 0) return false;
        out = std::atoi(text_.c_str() + begin);
        return true;
    }

    bool skip_value() {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '"') {
            std::string ignored;
            return string(ignored);
        }
        if (pos_ < text_.size() && text_[pos_] == '{') {
            if (!consume('{')) return false;
            skip_ws();
            if (consume('}')) return true;
            while (true) {
                skip_ws();
                std::string key;
                if (!string(key)) return false;
                skip_ws();
                if (!consume(':')) return false;
                if (!skip_value()) return false;
                skip_ws();
                if (consume('}')) return true;
                if (!consume(',')) return false;
            }
        }
        if (pos_ < text_.size() && text_[pos_] == '[') {
            if (!consume('[')) return false;
            skip_ws();
            if (consume(']')) return true;
            while (true) {
                if (!skip_value()) return false;
                skip_ws();
                if (consume(']')) return true;
                if (!consume(',')) return false;
                skip_ws();
            }
        }
        if (literal("true") || literal("false") || literal("null")) return true;
        int ignored = 0;
        return number(ignored);
    }

    bool literal(const char* word) {
        size_t length = std::char_traits<char>::length(word);
        if (text_.compare(pos_, length, word) != 0) return false;
        pos_ += length;
        return true;
    }

    bool top_object(SessionData& out) {
        if (!consume('{')) return false;
        skip_ws();
        if (consume('}')) return true;
        while (true) {
            skip_ws();
            std::string key;
            if (!string(key)) return false;
            skip_ws();
            if (!consume(':')) return false;
            skip_ws();
            if (key == "active") {
                if (!number(out.active)) return false;
            } else if (key == "tabs") {
                if (!tabs_array(out)) return false;
            } else {
                if (!skip_value()) return false;
            }
            skip_ws();
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
    }

    bool tabs_array(SessionData& out) {
        if (!consume('[')) return false;
        skip_ws();
        if (consume(']')) return true;
        while (true) {
            skip_ws();
            if (!tab_object(out)) return false;
            skip_ws();
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
    }

    bool tab_object(SessionData& out) {
        if (!consume('{')) return false;
        SessionTab tab;
        skip_ws();
        if (consume('}')) {
            out.tabs.push_back(std::move(tab));
            return true;
        }
        while (true) {
            skip_ws();
            std::string key;
            if (!string(key)) return false;
            skip_ws();
            if (!consume(':')) return false;
            skip_ws();
            if (key == "url" || key == "title" || key == "state") {
                std::string text;
                if (!string(text)) return false;
                if (key == "url") tab.url = text;
                else if (key == "title") tab.title = text;
                else tab.state = text;
            } else if (key == "back") {
                if (!array_string_vector(tab.back)) return false;
            } else if (key == "forward") {
                if (!array_string_vector(tab.forward)) return false;
            } else {
                if (!skip_value()) return false;
            }
            skip_ws();
            if (consume('}')) {
                out.tabs.push_back(std::move(tab));
                return true;
            }
            if (!consume(',')) return false;
        }
    }

    bool array_string_vector(std::vector<std::string>& out) {
        if (!consume('[')) return false;
        skip_ws();
        if (consume(']')) return true;
        while (true) {
            skip_ws();
            std::string item;
            if (!string(item)) return false;
            out.push_back(std::move(item));
            skip_ws();
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
    }

    std::string text_;
    size_t pos_ = 0;
};

}

bool load_session(const std::string& path, SessionData& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) return false;

    JsonParser parser(content);
    SessionData data;
    if (!parser.parse(data)) return false;

    if (data.active < 0 || data.active >= static_cast<int>(data.tabs.size())) {
        data.active = data.tabs.empty() ? 0 : static_cast<int>(data.tabs.size()) - 1;
    }
    out = std::move(data);
    return true;
}

bool save_session(const std::string& path, const SessionData& data) {
    std::ofstream out(path);
    if (!out) return false;
    out << "{\"version\":1,\"active\":" << data.active << ",\"tabs\":[";
    for (size_t i = 0; i < data.tabs.size(); ++i) {
        const SessionTab& tab = data.tabs[i];
        if (i) out << ",";
        out << "{\"url\":\"" << json_escape(tab.url) << "\"";
        out << ",\"title\":\"" << json_escape(tab.title) << "\"";
        out << ",\"state\":\"" << json_escape(tab.state) << "\"";
        out << ",\"back\":[";
        for (size_t j = 0; j < tab.back.size(); ++j) {
            if (j) out << ",";
            out << "\"" << json_escape(tab.back[j]) << "\"";
        }
        out << "],\"forward\":[";
        for (size_t j = 0; j < tab.forward.size(); ++j) {
            if (j) out << ",";
            out << "\"" << json_escape(tab.forward[j]) << "\"";
        }
        out << "]}";
    }
    out << "]}\n";
    return true;
}

}
