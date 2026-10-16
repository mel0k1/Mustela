#include "doh.hpp"

#include <cstdio>

namespace mustela {
namespace doh {

namespace {

void push_u16(std::vector<unsigned char>& out, unsigned short value) {
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value & 0xFF));
}

bool read_u16(const unsigned char* data, size_t size, size_t& offset, unsigned short& value) {
    if (offset + 2 > size) return false;
    value = static_cast<unsigned short>((data[offset] << 8) | data[offset + 1]);
    offset += 2;
    return true;
}

bool read_u32(const unsigned char* data, size_t size, size_t& offset, unsigned& value) {
    if (offset + 4 > size) return false;
    value = (static_cast<unsigned>(data[offset]) << 24) | (static_cast<unsigned>(data[offset + 1]) << 16) |
        (static_cast<unsigned>(data[offset + 2]) << 8) | static_cast<unsigned>(data[offset + 3]);
    offset += 4;
    return true;
}

bool skip_name(const unsigned char* data, size_t size, size_t& offset) {
    while (true) {
        if (offset >= size) return false;
        unsigned char length = data[offset];
        if (length == 0) {
            ++offset;
            return true;
        }
        if ((length & 0xC0) == 0xC0) {
            if (offset + 2 > size) return false;
            size_t pointer = (static_cast<size_t>(length & 0x3F) << 8) | data[offset + 1];
            if (pointer >= offset) return false;
            offset += 2;
            return true;
        }
        if (length > 63) return false;
        if (offset + 1 + length > size) return false;
        offset += 1 + length;
    }
}

bool read_label_bytes(const unsigned char* data, size_t size, size_t cursor, unsigned char length,
    std::string& name) {
    if (cursor + 1 + length > size) return false;
    if (!name.empty()) name += '.';
    for (unsigned i = 1; i <= length; ++i) {
        unsigned char c = data[cursor + i];
        if (c == '.' || c == '\\' || c < 33 || c > 126) return false;
        name.push_back(static_cast<char>(c));
    }
    return true;
}

bool read_name_at(const unsigned char* data, size_t size, size_t offset, std::string& out,
    size_t& consumed) {
    std::string name;
    size_t cursor = offset;
    size_t hops = 0;
    bool jumped = false;
    while (true) {
        if (cursor >= size) return false;
        unsigned char length = data[cursor];
        if (length == 0) {
            if (!jumped) consumed = cursor + 1 - offset;
            break;
        }
        if ((length & 0xC0) == 0xC0) {
            if (cursor + 2 > size) return false;
            size_t pointer = (static_cast<size_t>(length & 0x3F) << 8) | data[cursor + 1];
            if (pointer >= cursor || ++hops > 64) return false;
            if (!jumped) {
                consumed = cursor + 2 - offset;
                jumped = true;
            }
            cursor = pointer;
            continue;
        }
        if (length > 63) return false;
        if (!read_label_bytes(data, size, cursor, length, name)) return false;
        cursor += 1 + length;
    }
    out = name;
    return true;
}

bool format_ipv4(const unsigned char* rdata, size_t length, std::string& out) {
    if (length != 4) return false;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
        static_cast<unsigned>(rdata[0]), static_cast<unsigned>(rdata[1]),
        static_cast<unsigned>(rdata[2]), static_cast<unsigned>(rdata[3]));
    out = buffer;
    return true;
}

bool format_ipv6(const unsigned char* rdata, size_t length, std::string& out) {
    if (length != 16) return false;
    size_t best_start = 0;
    size_t best_length = 0;
    size_t run_start = 0;
    size_t run_length = 0;
    for (size_t i = 0; i < 8; ++i) {
        bool zero = rdata[i * 2] == 0 && rdata[i * 2 + 1] == 0;
        if (zero) {
            if (run_length == 0) run_start = i;
            ++run_length;
            if (run_length > best_length) {
                best_start = run_start;
                best_length = run_length;
            }
        } else {
            run_length = 0;
        }
    }
    if (best_length < 2) best_length = 0;
    char buffer[64] = {};
    size_t used = 0;
    for (size_t i = 0; i < 8;) {
        if (best_length > 0 && i == best_start) {
            used += static_cast<size_t>(std::snprintf(buffer + used, sizeof(buffer) - used, "::"));
            i += best_length;
            continue;
        }
        if (used && buffer[used - 1] != ':') {
            used += static_cast<size_t>(std::snprintf(buffer + used, sizeof(buffer) - used, ":"));
        }
        used += static_cast<size_t>(std::snprintf(buffer + used, sizeof(buffer) - used, "%x",
            (static_cast<unsigned>(rdata[i * 2]) << 8) | static_cast<unsigned>(rdata[i * 2 + 1])));
        ++i;
    }
    out = buffer;
    return true;
}

}

std::vector<unsigned char> build_query(const std::string& name, unsigned short type) {
    std::vector<unsigned char> out;
    if (name.empty()) return out;
    out.reserve(name.size() + 18);
    push_u16(out, 0);
    push_u16(out, 0x0100);
    push_u16(out, 1);
    push_u16(out, 0);
    push_u16(out, 0);
    push_u16(out, 0);
    size_t label_start = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '.') {
            size_t length = i - label_start;
            if (length == 0 || length > 63) return {};
            out.push_back(static_cast<unsigned char>(length));
            for (size_t j = 0; j < length; ++j) {
                unsigned char c = static_cast<unsigned char>(name[label_start + j]);
                if (c < 33 || c > 126 || c == '.') return {};
                out.push_back(static_cast<char>(c));
            }
            label_start = i + 1;
        }
    }
    out.push_back(0);
    push_u16(out, type);
    push_u16(out, 1);
    return out;
}

bool parse_response(const unsigned char* data, size_t size, Response& out) {
    if (!data || size < 12) return false;
    unsigned short flags = 0;
    size_t offset = 2;
    if (!read_u16(data, size, offset, flags)) return false;
    out.rcode = static_cast<Rcode>(flags & 0x0F);
    unsigned short question_count = 0;
    unsigned short answer_count = 0;
    if (!read_u16(data, size, offset, question_count)) return false;
    if (!read_u16(data, size, offset, answer_count)) return false;
    offset += 4;
    for (unsigned short i = 0; i < question_count; ++i) {
        if (!skip_name(data, size, offset)) return false;
        if (offset + 4 > size) return false;
        offset += 4;
    }
    out.answers.clear();
    for (unsigned short i = 0; i < answer_count; ++i) {
        Answer answer;
        std::string name;
        size_t consumed = 0;
        if (!read_name_at(data, size, offset, name, consumed)) return false;
        offset += consumed;
        unsigned short type = 0;
        unsigned short klass = 0;
        unsigned ttl = 0;
        unsigned short rdlength = 0;
        if (!read_u16(data, size, offset, type)) return false;
        if (!read_u16(data, size, offset, klass)) return false;
        if (!read_u32(data, size, offset, ttl)) return false;
        if (!read_u16(data, size, offset, rdlength)) return false;
        if (offset + rdlength > size) return false;
        answer.name = name;
        answer.type = type;
        answer.ttl = ttl;
        if (type == 1) {
            format_ipv4(data + offset, rdlength, answer.data);
        } else if (type == 28) {
            format_ipv6(data + offset, rdlength, answer.data);
        } else if (type == 2 || type == 5 || type == 12) {
            std::string target;
            size_t ignored = 0;
            if (read_name_at(data, size, offset, target, ignored)) answer.data = target;
        }
        offset += rdlength;
        out.answers.push_back(std::move(answer));
    }
    return true;
}

std::string encode_base64url(const std::vector<unsigned char>& bytes) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 3 <= bytes.size()) {
        unsigned value = (static_cast<unsigned>(bytes[i]) << 16) |
            (static_cast<unsigned>(bytes[i + 1]) << 8) | static_cast<unsigned>(bytes[i + 2]);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
        out.push_back(alphabet[value & 0x3F]);
        i += 3;
    }
    if (i + 1 == bytes.size()) {
        unsigned value = static_cast<unsigned>(bytes[i]) << 16;
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
    } else if (i + 2 == bytes.size()) {
        unsigned value = (static_cast<unsigned>(bytes[i]) << 16) |
            (static_cast<unsigned>(bytes[i + 1]) << 8);
        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        out.push_back(alphabet[(value >> 6) & 0x3F]);
    }
    return out;
}

std::string query_uri(const std::string& resolver, const std::string& name, unsigned short type) {
    if (resolver.empty()) return "";
    std::string payload = encode_base64url(build_query(name, type));
    if (payload.empty()) return "";
    return resolver + (resolver.find('?') == std::string::npos ? "?" : "&") + "dns=" + payload;
}

}
}
