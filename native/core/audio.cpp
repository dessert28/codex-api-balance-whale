#include "pch.h"
#include "audio.hpp"

#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace whale {

AudioPlayer::AudioPlayer(std::filesystem::path assetDir) : m_assetDir(std::move(assetDir)) {
    m_alias = "apiBalanceWhaleSound";
}

AudioPlayer::~AudioPlayer() {
    if (!m_openFile.empty()) {
        mciSendStringA(("close " + m_alias).c_str(), nullptr, 0, nullptr);
    }
}

std::filesystem::path AudioPlayer::Resolve(int set, bool press) const {
    const wchar_t* duck = press ? L"Ya1.mp3" : L"Ya2.mp3";
    const wchar_t* plain = press ? L"D1.mp3" : L"D2.mp3";
    return m_assetDir / (set == 1 ? duck : plain);
}

bool AudioPlayer::Available(int set) const {
    std::error_code error;
    return std::filesystem::exists(Resolve(set, true), error) &&
           std::filesystem::exists(Resolve(set, false), error);
}

bool AudioPlayer::Play(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) return false;
    if (m_openFile != file) {
        if (!m_openFile.empty()) {
            mciSendStringA(("close " + m_alias).c_str(), nullptr, 0, nullptr);
        }
        const auto narrow = file.string();
        const std::string open = "open \"" + narrow + "\" type mpegvideo alias " + m_alias;
        if (mciSendStringA(open.c_str(), nullptr, 0, nullptr) != 0) {
            m_openFile.clear();
            return false;
        }
        m_openFile = file;
    }
    mciSendStringA(("seek " + m_alias + " to start").c_str(), nullptr, 0, nullptr);
    return mciSendStringA(("play " + m_alias).c_str(), nullptr, 0, nullptr) == 0;
}

bool AudioPlayer::PlayPress(int set) {
    return Play(Resolve(set, true));
}

bool AudioPlayer::PlayRelease(int set) {
    return Play(Resolve(set, false));
}

} // namespace whale
