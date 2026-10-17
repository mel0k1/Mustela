#include "psl.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace mustela {

namespace {

#include "psl_data.inc"

bool rule_hit(const char* const* rules, size_t count, const std::string& key) {
    return count != 0 && std::binary_search(rules, rules + count, key);
}

std::string normalize_suffix_host(const std::string& host) {
    size_t begin = host.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = host.find_last_not_of(" \t\r\n");
    std::string text;
    text.reserve(end - begin + 1);
    for (size_t i = begin; i <= end; ++i) {
        text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(host[i]))));
    }
    while (!text.empty() && text.front() == '.') text.erase(text.begin());
    while (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

bool is_localhost_host(const std::string& host) {
    if (host == "localhost") return true;
    return host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0;
}

bool is_ip_literal_host(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    if (host.empty()) return false;
    return host.find_first_not_of("0123456789.") == std::string::npos;
}

std::vector<std::string> split_labels(const std::string& host) {
    std::vector<std::string> labels;
    size_t begin = 0;
    while (true) {
        size_t dot = host.find('.', begin);
        if (dot == std::string::npos) {
            labels.push_back(host.substr(begin));
            break;
        }
        labels.push_back(host.substr(begin, dot - begin));
        begin = dot + 1;
    }
    return labels;
}

std::string join_labels(const std::vector<std::string>& labels, size_t from) {
    if (from >= labels.size()) return "";
    std::string out = labels[from];
    for (size_t i = from + 1; i < labels.size(); ++i) {
        out.push_back('.');
        out += labels[i];
    }
    return out;
}

bool has_empty_label(const std::vector<std::string>& labels) {
    for (const std::string& label : labels) {
        if (label.empty()) return true;
    }
    return false;
}

size_t suffix_label_count(const std::vector<std::string>& labels) {
    size_t n = labels.size();
    for (size_t i = 0; i < n; ++i) {
        std::string candidate = join_labels(labels, i);
        if (rule_hit(kExceptionRules, kExceptionRulesCount, candidate)) return n - i - 1;
    }
    size_t best = 0;
    bool best_exact = false;
    for (size_t i = 0; i < n; ++i) {
        std::string candidate = join_labels(labels, i);
        bool exact = rule_hit(kExactRules, kExactRulesCount, candidate);
        bool wild = !candidate.empty() && rule_hit(kWildcardRules, kWildcardRulesCount, join_labels(labels, i + 1));
        if (!exact && !wild) continue;
        size_t rule_labels = n - i;
        if (best == 0 || rule_labels > best || (rule_labels == best && exact && !best_exact)) {
            best = rule_labels;
            best_exact = exact;
        }
    }
    return best == 0 ? 1 : best;
}

}

bool is_public_suffix(const std::string& raw) {
    std::string host = normalize_suffix_host(raw);
    if (host.empty() || is_ip_literal_host(host) || is_localhost_host(host)) return false;
    std::vector<std::string> labels = split_labels(host);
    if (labels.empty() || has_empty_label(labels)) return false;
    return suffix_label_count(labels) == labels.size();
}

std::string registrable_domain(const std::string& raw) {
    std::string host = normalize_suffix_host(raw);
    if (host.empty()) return "";
    if (is_ip_literal_host(host) || is_localhost_host(host)) return host;
    std::vector<std::string> labels = split_labels(host);
    if (labels.empty() || has_empty_label(labels)) return "";
    size_t suffix = suffix_label_count(labels);
    if (suffix >= labels.size()) return "";
    return join_labels(labels, labels.size() - suffix - 1);
}

std::string psl_version() {
    return kPslVersion;
}

}
