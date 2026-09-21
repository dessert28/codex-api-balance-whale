#pragma once

#include <filesystem>

namespace whale {

struct QuoteEditorPaths {
    std::filesystem::path configPath;
};

// Opens the random line editor (tray menu "随机语句…"). Blocks until the window
// closes; saving writes the `quotes` array into the overlay config.
int RunQuoteEditor(const QuoteEditorPaths& paths);

} // namespace whale
