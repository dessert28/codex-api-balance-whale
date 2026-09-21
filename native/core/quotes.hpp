#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <vector>

namespace whale {

// One random bubble line. The field names match the upstream widget, which keeps
// its editable line set as `{t, w}` entries.
struct QuoteLine {
    std::wstring text;
    int weight{1};
};

// Weight range accepted by the editor and the parser; upstream only clamps the
// lower bound, the upper bound keeps a typo from swallowing the whole set.
constexpr int kMaxQuoteWeight = 99;

// Built-in lines, used whenever overlay.json has no usable `quotes` entry.
std::vector<QuoteLine> DefaultQuotes();

// Editor text form: one quote per line as `weight|text`; a bare text line means
// weight 1 and blank lines are skipped. A '|' that is not preceded by a number is
// part of the text.
std::vector<QuoteLine> ParseQuoteText(const std::wstring& text);
std::wstring FormatQuoteText(const std::vector<QuoteLine>& lines);

// JSON form used by overlay.json: `[{"t":"...","w":2}, ...]`.
std::string QuoteJson(const std::vector<QuoteLine>& lines);
// Reads the `quotes` array out of a whole config text; empty when absent.
std::vector<QuoteLine> ParseQuoteJson(const std::string& configText);
// Span of the raw `[ ... ]` array, so a writer that does not understand quotes
// (the settings window) can copy it back verbatim.
bool FindQuoteSpan(const std::string& configText, std::size_t& begin, std::size_t& end);
// Replaces, or appends, the `quotes` array inside a config text.
std::string SetQuoteJson(const std::string& configText, const std::vector<QuoteLine>& lines);

// Weighted pick that avoids repeating `avoid` while there is a choice, mirroring
// the upstream `bubblePickLine`.
std::size_t PickQuote(const std::vector<QuoteLine>& lines, std::size_t avoid, std::mt19937& rng);

} // namespace whale
