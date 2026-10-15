#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mustela {

struct HistoryEntry {
    std::string url;
    std::string title;
    std::int64_t visited_at = 0;
};

class HistoryStore {
public:
    explicit HistoryStore(std::string path, std::size_t capacity = 5000);
    ~HistoryStore();

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    void record(const std::string& url, const std::string& title, std::int64_t visited_at);
    std::vector<HistoryEntry> recent(std::size_t limit) const;
    void clear();

private:
    void load();
    void save() const;

    std::string path_;
    std::size_t capacity_;
    std::vector<HistoryEntry> entries_;
};

}
