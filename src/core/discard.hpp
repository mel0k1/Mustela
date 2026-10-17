#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mustela {

enum class MemoryAction {
    Warn,
    Discard
};

MemoryAction memory_action_from(const std::string& name);
const char* memory_action_name(MemoryAction action);

struct DiscardableTab {
    std::size_t id = 0;
    bool active = false;
    bool discarded = false;
    bool loading = false;
    double idle_ms = 0;
};

struct DiscardPolicy {
    MemoryAction action = MemoryAction::Warn;
    int ceiling_mb = 0;
    std::int64_t total_bytes = 0;
    double min_idle_ms = 600000.0;
    std::size_t max_per_pass = 1;
    std::size_t min_alive = 1;
};

std::size_t alive_tab_count(const std::vector<DiscardableTab>& tabs);

std::vector<std::size_t> pick_discard_candidates(const std::vector<DiscardableTab>& tabs,
    const DiscardPolicy& policy);

}
