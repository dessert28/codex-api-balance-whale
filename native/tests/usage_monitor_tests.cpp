#include "../core/usage_monitor.hpp"

#include <cassert>
#include <chrono>
#include <iostream>

int main() {
    whale::UsageMonitor monitor;
    whale::UsageSnapshot first;
    first.stale = false;
    first.lastTurn = whale::LastTurn{"gpt-5.6-terra", 120, std::chrono::system_clock::from_time_t(100)};

    // The first scan establishes a baseline and must not replay old history.
    assert(!monitor.Update(first).has_value());

    // Re-reading the same latest turn must not notify twice.
    assert(!monitor.Update(first).has_value());

    whale::UsageSnapshot next = first;
    next.lastTurn = whale::LastTurn{"gpt-5.6-terra", 42, std::chrono::system_clock::from_time_t(101)};
    const auto notification = monitor.Update(next);
    assert(notification.has_value());
    assert(notification->model == "gpt-5.6-terra");
    assert(notification->deltaTokens == 42);

    // A stale / failed snapshot does not erase the baseline or create a notice.
    whale::UsageSnapshot stale;
    stale.stale = true;
    stale.error = "暂无数据";
    assert(!monitor.Update(stale).has_value());
    assert(!monitor.Update(next).has_value());

    std::cout << "usage monitor tests passed\n";
}
