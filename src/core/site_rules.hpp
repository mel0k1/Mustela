#pragma once

#include <map>
#include <string>
#include <vector>

namespace mustela {

enum class SiteRule {
    Default,
    Allow,
    Deny
};

struct SitePolicy {
    SiteRule javascript = SiteRule::Default;
    SiteRule cookies = SiteRule::Default;
    SiteRule fingerprint = SiteRule::Default;
    SiteRule permissions = SiteRule::Default;
    double zoom = 0.0;
    std::string user_agent;
    std::string css;
};

const char* site_rule_name(SiteRule rule);
SiteRule site_rule_from(const std::string& name);
std::string host_from_uri(const std::string& uri);

class SiteRules {
public:
    struct Entry {
        std::string host;
        SitePolicy policy;
    };

    explicit SiteRules(std::string path);

    void load();
    void save() const;

    const SitePolicy* find(const std::string& host) const;
    SitePolicy policy_for(const std::string& host) const;
    void set(const std::string& host, const SitePolicy& policy);
    void remove(const std::string& host);
    std::vector<Entry> entries() const;

private:
    std::string path_;
    std::map<std::string, SitePolicy> rules_;
};

}
