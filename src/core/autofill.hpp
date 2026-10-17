#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mustela {

struct AutofillEntry {
    std::string domain;
    std::string field;
    std::string value;
    int uses = 0;
    std::int64_t last_used = 0;
};

class AutofillStore {
public:
    explicit AutofillStore(std::string path);

    const std::string& path() const { return path_; }

    void load();
    void save() const;

    std::vector<AutofillEntry> find(const std::string& domain, const std::string& field,
        size_t limit = 5) const;
    void upsert(const std::string& domain, const std::string& field, const std::string& value,
        std::int64_t now);
    bool remove(const std::string& domain, const std::string& field, const std::string& value);
    void clear();
    size_t size() const { return entries_.size(); }

private:
    std::string path_;
    std::vector<AutofillEntry> entries_;
};

}
