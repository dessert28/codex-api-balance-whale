#pragma once

#include "usage_snapshot.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace whale {

// Live value for one `{token}` that may appear inside a random bubble line. This
// mirrors the upstream `bubbleContentTokenMap`: only known names are substituted,
// an unknown `{...}` stays verbatim, and a reading we cannot produce renders as
// `--` (upstream shows `…` / `--` for money it cannot read).
struct BubbleToken {
    std::wstring name;
    std::wstring value;
};

// Names understood by this build, in the order the editor lists them. The native
// widget has no balance ledger, so these carry the quota and token numbers it does
// have instead of the upstream money figures (`balance_api` / `expense_api`).
const std::vector<std::wstring>& BubbleTokenNames();

// `{p5h} {week} {reset5h} {resetweek} {turn} {today} {tokens7d} {model} {date} {time}`
std::wstring BubbleTokenListText();

// Values for every name above. `now` drives the countdown and clock tokens.
std::vector<BubbleToken> BubbleTokenValues(const UsageSnapshot& snapshot,
                                           std::chrono::system_clock::time_point now);

// Substitutes `{name}` occurrences in `text`; longest name first, like upstream.
std::wstring ExpandBubbleTokens(const std::wstring& text, const std::vector<BubbleToken>& tokens);

} // namespace whale
