#pragma once

#include "../core/usage_monitor.hpp"
#include "../core/usage_snapshot.hpp"

#include <filesystem>

namespace whale {

// Overlay window class and reload message shared with the settings window.
extern const wchar_t* const kOverlayWindowClass;
extern const wchar_t* const kOverlayReloadMessage;

struct OverlayOptions {
    std::filesystem::path codexHome;
    std::filesystem::path statePath;
    std::filesystem::path configPath;
    std::filesystem::path assetPath;
    int size{440};
    bool soundEnabled{true};
    int soundSet{0};
    int hideSeconds{5};
    bool hasPosition{false};
    int x{};
    int y{};
};

// Runs the desktop whale on a per-pixel alpha layered window. The call
// blocks until the overlay window closes, so run it on the thread that
// should own the message loop.
int RunDesktopOverlay(const OverlayOptions& options);

} // namespace whale
