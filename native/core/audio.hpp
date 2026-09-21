#pragma once

#include <filesystem>
#include <string>

namespace whale {

// Minimal MCI based MP3 player: one clip at a time, silent when the file or
// the codec is unavailable, matching the upstream widget's graceful fallback.
class AudioPlayer {
public:
    explicit AudioPlayer(std::filesystem::path assetDir);
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // set: 0 = built-in whale sounds (D1/D2), 1 = duck sounds (Ya1/Ya2).
    // Both report false when the clip is missing or could not be started, so a
    // caller can tell "played" from "silently did nothing".
    bool PlayPress(int set);
    bool PlayRelease(int set);
    bool Available(int set) const;

private:
    bool Play(const std::filesystem::path& file);
    std::filesystem::path Resolve(int set, bool press) const;

    std::filesystem::path m_assetDir;
    std::string m_alias;
    std::filesystem::path m_openFile;
};

} // namespace whale
