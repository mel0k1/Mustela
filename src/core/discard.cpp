#include "discard.hpp"

#include <algorithm>
#include <numeric>

#include "perf.hpp"

namespace mustela {

MemoryAction memory_action_from(const std::string& name) {
    if (name == "discard") return MemoryAction::Discard;
    return MemoryAction::Warn;
}

const char* memory_action_name(MemoryAction action) {
    return action == MemoryAction::Discard ? "discard" : "warn";
}

std::size_t alive_tab_count(const std::vector<DiscardableTab>& tabs) {
    std::size_t alive = 0;
    for (const DiscardableTab& tab : tabs) {
        if (!tab.discarded) ++alive;
    }
    return alive;
}

std::vector<std::size_t> pick_discard_candidates(const std::vector<DiscardableTab>& tabs,
    const DiscardPolicy& policy) {
    if (policy.action != MemoryAction::Discard) return {};
    if (policy.ceiling_mb <= 0) return {};
    if (!ceiling_exceeded(policy.ceiling_mb, policy.total_bytes)) return {};

    std::size_t alive = alive_tab_count(tabs);
    std::size_t budget = policy.max_per_pass;
    if (policy.min_alive > alive) return {};
    std::size_t discardable = alive - policy.min_alive;
    if (budget > discardable) budget = discardable;
    if (budget == 0) return {};

    std::vector<const DiscardableTab*> eligible;
    for (const DiscardableTab& tab : tabs) {
        if (tab.active || tab.discarded || tab.loading) continue;
        if (tab.idle_ms < policy.min_idle_ms) continue;
        eligible.push_back(&tab);
    }

    std::stable_sort(eligible.begin(), eligible.end(),
        [](const DiscardableTab* a, const DiscardableTab* b) { return a->idle_ms > b->idle_ms; });

    std::vector<std::size_t> picked;
    picked.reserve(budget);
    for (const DiscardableTab* tab : eligible) {
        if (picked.size() >= budget) break;
        picked.push_back(tab->id);
    }
    return picked;
}

}
