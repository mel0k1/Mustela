#pragma once

#include <cstdint>

namespace mustela {

enum class DataKind : std::uint32_t {
    None = 0,
    Cookies = 1u << 0,
    Cache = 1u << 1,
    LocalStorage = 1u << 2,
    IndexedDB = 1u << 3,
    All = Cookies | Cache | LocalStorage | IndexedDB
};

constexpr DataKind operator|(DataKind a, DataKind b) {
    return static_cast<DataKind>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr DataKind operator&(DataKind a, DataKind b) {
    return static_cast<DataKind>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

constexpr bool has(DataKind mask, DataKind bit) {
    return (mask & bit) != DataKind::None;
}

struct ClearRequest {
    DataKind kinds = DataKind::All;
    std::int64_t max_age_seconds = 0;
};

}
