#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mustela {

struct BookmarkEntry {
    std::string url;
    std::string title;
    std::int64_t added_at = 0;
};

class BookmarkStore {
public:
    explicit BookmarkStore(std::string path);
    ~BookmarkStore();

    BookmarkStore(const BookmarkStore&) = delete;
    BookmarkStore& operator=(const BookmarkStore&) = delete;

    bool add(const std::string& url, const std::string& title, std::int64_t added_at);
    bool remove(const std::string& url);
    bool contains(const std::string& url) const;
    const std::vector<BookmarkEntry>& entries() const { return entries_; }

private:
    void load();
    void save() const;

    std::string path_;
    std::vector<BookmarkEntry> entries_;
};

}
