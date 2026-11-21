#pragma once
#include "core.hpp"

namespace dc {
// Only the explicitly selected group participates. Other unread items provide
// no evidence for keeping a copy. Verification is repeated at execution.
Plan plan_reviewed_duplicate_group(const Inventory& inventory,const std::vector<fs::path>& selected,
                                  RetentionPolicy policy={},const std::atomic_bool* cancel=nullptr);
}
