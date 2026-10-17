#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bookmarks.hpp"

namespace mustela {

struct DetectedProfile {
    std::string browser;
    std::string profile;
    std::string path;
    bool has_bookmarks = false;
};

struct ImportReport {
    int bookmarks_added = 0;
    int bookmarks_skipped = 0;
    std::string error;
};

std::vector<DetectedProfile> detect_browsers();

std::vector<DetectedProfile> detect_browsers_in(const std::string& home);

ImportReport import_bookmarks_chromium(const std::string& profile_dir, BookmarkStore& into);

ImportReport import_bookmarks_firefox(const std::string& profile_dir, BookmarkStore& into);

ImportReport import_bookmarks_html(const std::string& html_path, BookmarkStore& into);

std::int64_t chrome_time_to_unix(std::int64_t value);

std::int64_t firefox_time_to_unix(std::int64_t value);

}
