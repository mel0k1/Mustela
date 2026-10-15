#include "site_rules.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>

namespace mustela {

namespace {

std::string trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}

const char* site_rule_name(SiteRule rule) {
    switch (rule) {
        case SiteRule::Allow: return "allow";
        case SiteRule::Deny: return "deny";
        case SiteRule::Default: return "default";
    }
    return "default";
}

SiteRule site_rule_from(const std::string& name) {
    if (name == "allow") return SiteRule::Allow;
    if (name == "deny") return SiteRule::Deny;
    return SiteRule::Default;
}

std::string host_from_uri(const std::string& uri) {
    std::string text = trim(uri);
    size_t scheme = text.find("://");
    if (scheme != std::string::npos) text = text.substr(scheme + 3);
    size_t end = text.find_first_of("/?#");
    if (end != std::string::npos) text = text.substr(0, end);
    size_t at = text.rfind('@');
    if (at != std::string::npos) text = text.substr(at + 1);
    if (!text.empty() && text.front() == '[') {
        size_t close = text.find(']');
        text = close == std::string::npos ? text.substr(1) : text.substr(1, close - 1);
    } else {
        size_t colon = text.rfind(':');
        if (colon != std::string::npos) text = text.substr(0, colon);
    }
    return to_lower(text);
}

SiteRules::SiteRules(std::string path) : path_(std::move(path)) {}

void SiteRules::load() {
    rules_.clear();
    std::ifstream in(path_);
    if (!in) return;

    std::string line;
    std::string host;
    while (std::getline(in, line)) {
        std::string text = trim(line);
        if (text.empty() || text[0] == '#') continue;

        if (text.front() == '[' && text.back() == ']') {
            host = to_lower(trim(text.substr(1, text.size() - 2)));
            continue;
        }

        size_t split = text.find('=');
        if (split == std::string::npos || host.empty()) continue;

        std::string key = trim(text.substr(0, split));
        std::string value = trim(text.substr(split + 1));
        if (!value.empty() && value.front() == '"') {
            size_t closing = value.find('"', 1);
            if (closing == std::string::npos) continue;
            value = value.substr(1, closing - 1);
        } else {
            size_t comment = value.find('#');
            if (comment != std::string::npos) value = trim(value.substr(0, comment));
        }

        if (key == "javascript") rules_[host].javascript = site_rule_from(value);
        if (key == "cookies") rules_[host].cookies = site_rule_from(value);
        if (key == "fingerprint") rules_[host].fingerprint = site_rule_from(value);
    }
}

void SiteRules::save() const {
    std::ofstream out(path_);
    if (!out) return;

    for (const auto& [host, policy] : rules_) {
        if (policy.javascript == SiteRule::Default && policy.cookies == SiteRule::Default
            && policy.fingerprint == SiteRule::Default) continue;
        out << "[" << host << "]\n";
        if (policy.javascript != SiteRule::Default) {
            out << "javascript = \"" << site_rule_name(policy.javascript) << "\"\n";
        }
        if (policy.cookies != SiteRule::Default) {
            out << "cookies = \"" << site_rule_name(policy.cookies) << "\"\n";
        }
        if (policy.fingerprint != SiteRule::Default) {
            out << "fingerprint = \"" << site_rule_name(policy.fingerprint) << "\"\n";
        }
        out << "\n";
    }
}

const SitePolicy* SiteRules::find(const std::string& host) const {
    auto it = rules_.find(to_lower(trim(host)));
    return it == rules_.end() ? nullptr : &it->second;
}

SitePolicy SiteRules::policy_for(const std::string& host) const {
    std::string key = to_lower(trim(host));
    auto exact = rules_.find(key);
    if (exact != rules_.end()) return exact->second;

    for (size_t dot = key.find('.'); dot != std::string::npos; dot = key.find('.', dot + 1)) {
        auto it = rules_.find(key.substr(dot + 1));
        if (it != rules_.end()) return it->second;
    }
    return SitePolicy{};
}

void SiteRules::set(const std::string& host, const SitePolicy& policy) {
    std::string key = to_lower(trim(host));
    if (key.empty()) return;
    if (policy.javascript == SiteRule::Default && policy.cookies == SiteRule::Default
        && policy.fingerprint == SiteRule::Default) {
        rules_.erase(key);
        return;
    }
    rules_[key] = policy;
}

void SiteRules::remove(const std::string& host) {
    rules_.erase(to_lower(trim(host)));
}

std::vector<SiteRules::Entry> SiteRules::entries() const {
    std::vector<Entry> out;
    for (const auto& [host, policy] : rules_) {
        if (policy.javascript == SiteRule::Default && policy.cookies == SiteRule::Default
            && policy.fingerprint == SiteRule::Default) continue;
        out.push_back(Entry{host, policy});
    }
    return out;
}

}
