#pragma once

namespace whale {

// Which bubble is on screen. The upstream widget gives every scene its own
// lifetime instead of one shared auto-hide delay.
enum class BubbleKind {
    Quote,
    Quota,
    Turn,
};

struct BubblePolicy {
    int hideSeconds{5};
    int turnSeconds{6};
    bool autoClose{true};
};

// Upstream keeps the manual quota card on screen for 10 s, a per-turn usage
// reminder for ttlSec (6 s by default) and a plain line for the configured
// bubble delay; turning auto close off keeps the card until the next click.
inline int BubbleTtlMs(BubbleKind kind, const BubblePolicy& policy) {
    if (!policy.autoClose) return 0;
    switch (kind) {
    case BubbleKind::Quota:
        return 10000;
    case BubbleKind::Turn:
        return policy.turnSeconds > 0 ? policy.turnSeconds * 1000 : 6000;
    case BubbleKind::Quote:
    default:
        return policy.hideSeconds > 0 ? policy.hideSeconds * 1000 : 5000;
    }
}

} // namespace whale
