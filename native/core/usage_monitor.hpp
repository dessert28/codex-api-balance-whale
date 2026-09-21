#pragma once

#include "usage_snapshot.hpp"

#include <optional>

namespace whale {

class UsageMonitor {
public:
    std::optional<LastTurn> Update(const UsageSnapshot& snapshot);

private:
    std::optional<LastTurn> m_latest;
};

} // namespace whale
