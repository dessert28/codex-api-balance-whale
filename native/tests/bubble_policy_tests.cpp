#include "../core/bubble_policy.hpp"

#include <cassert>
#include <iostream>

int main() {
    whale::BubblePolicy policy;

    // The manual quota card keeps the upstream 10 s lifetime.
    assert(whale::BubbleTtlMs(whale::BubbleKind::Quota, policy) == 10000);
    // A random line uses the configured bubble delay.
    assert(whale::BubbleTtlMs(whale::BubbleKind::Quote, policy) == 5000);
    // The per-turn notice uses its own delay so tuning one does not move the
    // other.
    assert(whale::BubbleTtlMs(whale::BubbleKind::Turn, policy) == 6000);

    policy.hideSeconds = 12;
    policy.turnSeconds = 9;
    assert(whale::BubbleTtlMs(whale::BubbleKind::Quote, policy) == 12000);
    assert(whale::BubbleTtlMs(whale::BubbleKind::Turn, policy) == 9000);
    assert(whale::BubbleTtlMs(whale::BubbleKind::Quota, policy) == 10000);

    // Auto close off means the bubble stays until the user clicks it away.
    policy.autoClose = false;
    assert(whale::BubbleTtlMs(whale::BubbleKind::Quota, policy) == 0);
    assert(whale::BubbleTtlMs(whale::BubbleKind::Quote, policy) == 0);
    assert(whale::BubbleTtlMs(whale::BubbleKind::Turn, policy) == 0);

    // Unset or out-of-range delays fall back to the documented defaults.
    policy.autoClose = true;
    policy.hideSeconds = 0;
    policy.turnSeconds = 0;
    assert(whale::BubbleTtlMs(whale::BubbleKind::Quote, policy) == 5000);
    assert(whale::BubbleTtlMs(whale::BubbleKind::Turn, policy) == 6000);

    std::cout << "bubble policy tests passed\n";
}
