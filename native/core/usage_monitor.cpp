#include "usage_monitor.hpp"

namespace whale {

std::optional<LastTurn> UsageMonitor::Update(const UsageSnapshot& snapshot) {
    if (!snapshot.lastTurn || snapshot.stale) return std::nullopt;
    const auto& current = *snapshot.lastTurn;
    if (!m_latest) {
        m_latest = current;
        return std::nullopt;
    }
    if (current.timestamp <= m_latest->timestamp && current.deltaTokens == m_latest->deltaTokens && current.model == m_latest->model) {
        return std::nullopt;
    }
    m_latest = current;
    return current;
}

} // namespace whale
