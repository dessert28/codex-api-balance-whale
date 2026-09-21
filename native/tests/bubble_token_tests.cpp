#include "../core/bubble_tokens.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::wstring ValueOf(const std::vector<whale::BubbleToken>& tokens, const std::wstring& name) {
    for (const auto& token : tokens) {
        if (token.name == name) return token.value;
    }
    return L"<missing token>";
}

} // namespace

int main() {
    const auto now = std::chrono::system_clock::now();
    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // Names and values must stay in step: the editor lists the names, the overlay
    // expands whatever the values carry.
    const auto names = whale::BubbleTokenNames();
    assert(names.size() == 10);
    assert(names.front() == L"p5h");
    assert(names.back() == L"time");

    whale::UsageSnapshot snapshot;
    snapshot.todayTokens = 12345;
    snapshot.last7dTokens = 987654;
    snapshot.currentModel = "gpt-5-current";
    whale::UsageWindow fiveHour;
    fiveHour.usedPercent = 13.4;
    fiveHour.resetsAt = nowSeconds + (2 * 3600 + 13 * 60);
    snapshot.fiveHour = fiveHour;
    whale::UsageWindow weekly;
    weekly.usedPercent = 55.6;
    weekly.resetsAt = nowSeconds + (3 * 86400 + 4 * 3600);
    snapshot.weekly = weekly;
    whale::LastTurn turn;
    turn.model = "gpt-5-codex";
    turn.deltaTokens = 3602;
    snapshot.lastTurn = turn;

    const auto values = whale::BubbleTokenValues(snapshot, now);
    assert(values.size() == names.size());
    for (std::size_t index = 0; index < names.size(); ++index) assert(values[index].name == names[index]);

    // Percentages round like the quota card, and the countdown drops 后重置.
    assert(ValueOf(values, L"p5h") == L"13%");
    assert(ValueOf(values, L"week") == L"56%");
    assert(ValueOf(values, L"reset5h") == L"2小时13分钟");
    assert(ValueOf(values, L"resetweek") == L"3天4小时");
    // A finished turn wins over the model that is still writing.
    assert(ValueOf(values, L"turn") == L"3602");
    assert(ValueOf(values, L"model") == L"gpt-5-codex");
    assert(ValueOf(values, L"today") == L"12345");
    assert(ValueOf(values, L"tokens7d") == L"987654");

    // Without a finished turn the model falls back to the running one, and the turn
    // counter has nothing to show.
    whale::UsageSnapshot early;
    early.currentModel = "gpt-5-codex";
    const auto earlyValues = whale::BubbleTokenValues(early, now);
    assert(ValueOf(earlyValues, L"model") == L"gpt-5-codex");
    assert(ValueOf(earlyValues, L"turn") == L"--");
    assert(ValueOf(earlyValues, L"p5h") == L"--");
    assert(ValueOf(earlyValues, L"resetweek") == L"--");
    assert(ValueOf(earlyValues, L"today") == L"0");

    // An empty snapshot has no model at all, and an elapsed window is not a reset.
    const auto emptyValues = whale::BubbleTokenValues(whale::UsageSnapshot{}, now);
    assert(ValueOf(emptyValues, L"model") == L"--");
    whale::UsageSnapshot expired;
    whale::UsageWindow past;
    past.usedPercent = 4.4;
    past.resetsAt = nowSeconds - 60;
    expired.fiveHour = past;
    const auto expiredValues = whale::BubbleTokenValues(expired, now);
    assert(ValueOf(expiredValues, L"p5h") == L"4%");
    assert(ValueOf(expiredValues, L"reset5h") == L"--");

    // Clock tokens follow the local time, so check the shape instead of a zone.
    const auto date = ValueOf(values, L"date");
    const auto time = ValueOf(values, L"time");
    assert(date.size() == 10 && date[4] == L'-' && date[7] == L'-');
    assert(time.size() == 5 && time[2] == L':');

    // Expansion: known tokens anywhere in the line, unknown ones left verbatim - the
    // upstream loop only replaces keys it knows, so an imported `{balance_api}` line
    // keeps its text instead of collapsing into an empty string.
    assert(whale::ExpandBubbleTokens(L"5 小时已用 {p5h}", values) == L"5 小时已用 13%");
    assert(whale::ExpandBubbleTokens(L"{p5h}/{week} {p5h}", values) == L"13%/56% 13%");
    assert(whale::ExpandBubbleTokens(L"余额 {balance_api}", values) == L"余额 {balance_api}");
    assert(whale::ExpandBubbleTokens(L"{P5H}", values) == L"{P5H}");
    assert(whale::ExpandBubbleTokens(L"没有占位符", values) == L"没有占位符");
    assert(whale::ExpandBubbleTokens(L"", values).empty());
    // A longer name is never clipped by a shorter one that shares its letters.
    assert(whale::ExpandBubbleTokens(L"{resetweek}", values) == L"3天4小时");
    // A lone brace is not a token.
    assert(whale::ExpandBubbleTokens(L"{p5h", values) == L"{p5h");

    // Substitution is one pass, longest name first, exactly like the upstream loop:
    // a value that itself contains a placeholder is filled in by a later token, and
    // a self-referencing value cannot loop forever.
    const std::vector<whale::BubbleToken> nested{{L"week", L"{p5h}"}, {L"p5h", L"13%"}};
    assert(whale::ExpandBubbleTokens(L"{week} {p5h}", nested) == L"13% 13%");
    const std::vector<whale::BubbleToken> selfRef{{L"a", L"{a}"}};
    assert(whale::ExpandBubbleTokens(L"{a}", selfRef) == L"{a}");

    // The editor hint lists every placeholder in the documented order.
    assert(whale::BubbleTokenListText() ==
           L"{p5h} {week} {reset5h} {resetweek} {turn} {today} {tokens7d} {model} {date} {time}");

    std::cout << "native bubble token tests passed\n";
}
