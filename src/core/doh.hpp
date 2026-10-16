#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mustela {
namespace doh {

enum class Rcode : unsigned char {
    NoError = 0,
    FormatError = 1,
    ServerFailure = 2,
    NameError = 3,
    NotImplemented = 4,
    Refused = 5
};

struct Answer {
    std::string name;
    unsigned short type = 0;
    unsigned ttl = 0;
    std::string data;
};

struct Response {
    Rcode rcode = Rcode::NoError;
    std::vector<Answer> answers;
};

std::vector<unsigned char> build_query(const std::string& name, unsigned short type = 1);
bool parse_response(const unsigned char* data, size_t size, Response& out);
std::string encode_base64url(const std::vector<unsigned char>& bytes);
std::string query_uri(const std::string& resolver, const std::string& name, unsigned short type = 1);

}
}
