#include "../core/quotes.hpp"

#include <cassert>
#include <iostream>
#include <random>
#include <string>

int main() {
    // Text form: weight prefix, bare text, skipped blanks, '|' inside the text.
    const auto parsed = whale::ParseQuoteText(L"3|主句\r\n\r\n  备用句子  \r\n多|个竖线\r\n7|\r\n");
    assert(parsed.size() == 3);
    assert(parsed[0].text == L"主句" && parsed[0].weight == 3);
    assert(parsed[1].text == L"备用句子" && parsed[1].weight == 1);
    assert(parsed[2].text == L"多|个竖线" && parsed[2].weight == 1);

    // Format and parse round trip.
    assert(whale::ParseQuoteText(whale::FormatQuoteText(parsed)).size() == parsed.size());

    // 0 and 1000 are clamped into the editor range.
    const auto clamped = whale::ParseQuoteText(L"0|低\r\n1000|高");
    assert(clamped[0].weight == 1 && clamped[1].weight == whale::kMaxQuoteWeight);

    // JSON round trip, including quotes, backslashes and non-ASCII text.
    const std::vector<whale::QuoteLine> lines{{L"他说\"你好\"", 2}, {L"C:\\codex\\rollout", 1}, {L"换行\n第二行", 5}};
    const auto json = whale::QuoteJson(lines);
    const auto back = whale::ParseQuoteJson("{\"size\":440,\"quotes\":" + json + ",\"turnNotice\":1}");
    assert(back.size() == lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        assert(back[index].text == lines[index].text);
        assert(back[index].weight == lines[index].weight);
    }

    // A config without quotes yields nothing; SetQuoteJson appends the array and
    // keeps the keys around it.
    assert(whale::ParseQuoteJson("{\"size\":440}").empty());
    // Braces inside a line's text - the upstream placeholders - must not be read as
    // object structure: a naive scan for '{' / '}' stopped the object at `{p5h}` and
    // the whole set came back empty (which silently fell back to the built-in lines).
    const std::string braceConfig =
        "{\"size\":440,\"quotes\":[{\"t\":\"5h={p5h} {week}\",\"w\":3},{\"t\":\"余额 {balance_api}\",\"w\":1}],\"turnNotice\":1}";
    const auto braceLines = whale::ParseQuoteJson(braceConfig);
    assert(braceLines.size() == 2);
    assert(braceLines[0].text == L"5h={p5h} {week}" && braceLines[0].weight == 3);
    assert(braceLines[1].text == L"余额 {balance_api}" && braceLines[1].weight == 1);
    std::size_t braceBegin = 0;
    std::size_t braceEnd = 0;
    assert(whale::FindQuoteSpan(braceConfig, braceBegin, braceEnd));
    assert(braceConfig.substr(braceBegin, braceEnd - braceBegin) ==
           "[{\"t\":\"5h={p5h} {week}\",\"w\":3},{\"t\":\"余额 {balance_api}\",\"w\":1}]");
    // A rewrite through the span-preserving writer keeps them too.
    assert(whale::ParseQuoteJson(whale::SetQuoteJson(braceConfig, braceLines)).size() == 2);

    const auto appended = whale::SetQuoteJson("{\"size\":440}\n", std::vector<whale::QuoteLine>{{L"句子", 4}});
    assert(appended.find("\"size\":440") != std::string::npos);
    const auto appendedLines = whale::ParseQuoteJson(appended);
    assert(appendedLines.size() == 1 && appendedLines[0].weight == 4);

    const auto replaced = whale::SetQuoteJson(
        "{\"size\":440,\"quotes\":" + whale::QuoteJson(std::vector<whale::QuoteLine>{{L"旧", 1}, {L"更旧", 1}}) + ",\"turnNotice\":1}",
        std::vector<whale::QuoteLine>{{L"新", 9}});
    const auto replacedLines = whale::ParseQuoteJson(replaced);
    assert(replacedLines.size() == 1 && replacedLines[0].text == L"新" && replacedLines[0].weight == 9);
    assert(replaced.find("\"turnNotice\"") != std::string::npos);
    assert(replaced.find(u8"更旧") == std::string::npos);

    // Weighted pick: the heavy line dominates.
    std::mt19937 rng(1234);
    const std::vector<whale::QuoteLine> weighted{{L"rare", 1}, {L"common", 9}};
    int common = 0;
    for (int attempt = 0; attempt < 400; ++attempt) {
        if (whale::PickQuote(weighted, weighted.size(), rng) == 1) ++common;
    }
    assert(common > 250);

    // The line that was just shown is avoided whenever the rolls allow it.
    int rare = 0;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (whale::PickQuote(weighted, 1, rng) == 0) ++rare;
    }
    assert(rare > 20);

    // With even weights a repeat is rare: upstream retries six times before it
    // accepts showing the same line twice.
    std::mt19937 avoidRng(99);
    const std::vector<whale::QuoteLine> even{{L"a", 1}, {L"b", 1}, {L"c", 1}};
    std::size_t previous = 0;
    int repeats = 0;
    for (int attempt = 0; attempt < 300; ++attempt) {
        const auto pick = whale::PickQuote(even, previous, avoidRng);
        if (pick == previous) ++repeats;
        previous = pick;
    }
    assert(repeats < 30);

    assert(whale::PickQuote({}, 0, rng) == 0);
    assert(whale::PickQuote({{L"only", 1}}, 0, rng) == 0);
    assert(whale::DefaultQuotes().size() == 10);

    std::cout << "native quote tests passed\n";
}
