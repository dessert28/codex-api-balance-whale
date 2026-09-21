#pragma once

#include <filesystem>

namespace whale {

struct SettingsPaths {
    std::filesystem::path configPath;
    std::filesystem::path assetPath;
};

// Opens the native settings window. Blocks until the window closes.
int RunSettingsWindow(const SettingsPaths& paths);

} // namespace whale
