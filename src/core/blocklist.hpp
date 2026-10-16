#pragma once

#include <string>
#include <vector>

namespace mustela {

class Blocklist {
public:
    Blocklist();

    void add(const std::string& domain);
    void clear();
    bool hit(const std::string& host) const;
    const std::vector<std::string>& domains() const { return domains_; }
    size_t size() const { return domains_.size(); }

private:
    std::vector<std::string> domains_;
};

std::string normalize_host(const std::string& host);
bool host_matches_domain(const std::string& host, const std::string& domain);
Blocklist default_blocklist(const std::string& extra_file = "");
std::string blocklist_rules_json(const std::vector<std::string>& domains);

}
