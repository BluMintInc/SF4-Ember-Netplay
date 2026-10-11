#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// A call back from Training, or the opponent at the player's table readying,
// while the game's window is behind another or minimized. The game mutes
// itself there unless Play in the background is on, and its announcer only
// speaks at the main menu, so the player would miss both. Ember then flashes
// the window's taskbar button and plays a chime of its own through Windows,
// never through the game's mixer (sf4e__AwayAlert.cxx). In front nothing
// changes.
namespace sf4e { namespace away_alert {

// What came in: each source numbers its own events, and a key carries both.
enum class Source : std::uint8_t { TrainingCall = 1, ReadyChallenge = 2 };
inline std::uint64_t Key(Source source, std::uint64_t serial) {
    return (static_cast<std::uint64_t>(source) << 56) | (serial & ((std::uint64_t(1) << 56) - 1));
}

// Says whether a call or a challenge is announced outside the game. Each is
// announced at most once (its key, never 0), and only while the window is
// away when it comes in: one that came in front stays unannounced if the
// player leaves afterwards. A second one within QuietMs of the last alert is
// not announced either, so a call and the ready of the same opponent
// sitting down make one alert.
class Alert {
public:
    static constexpr std::uint64_t QuietMs = 4000;
    bool Ring(std::uint64_t key, bool away, std::uint64_t nowMs) {
        if (!key || key == lastKey_) return false;
        lastKey_ = key;
        if (!away || (rang_ && nowMs - rangAtMs_ < QuietMs)) return false;
        rang_ = true; rangAtMs_ = nowMs;
        return true;
    }
private:
    std::uint64_t lastKey_ = 0, rangAtMs_ = 0;
    bool rang_ = false;
};

// The chime: two short rising tones, a 16-bit mono WAV file in memory for
// PlaySoundW, at volumePercent (10 to 100) of its full level.
inline std::vector<std::uint8_t> Chime(int volumePercent) {
    constexpr std::uint32_t Rate = 22050;
    struct Tone { double hz, seconds; };
    const Tone tones[] = { {880.0, .12}, {1318.5, .22} };
    const double level = 0.6 * 32767.0 * ((volumePercent < 10 ? 10 : volumePercent > 100 ? 100 : volumePercent) / 100.0);
    std::vector<std::int16_t> samples;
    for (const auto& tone : tones) {
        const auto count = static_cast<std::uint32_t>(tone.seconds * Rate);
        for (std::uint32_t i = 0; i < count; ++i) {
            const double t = static_cast<double>(i) / Rate;
            // A 5 ms rise, then a fall to silence by the tone's end.
            const double rise = t < .005 ? t / .005 : 1.0;
            const double fall = std::exp(-4.0 * t / tone.seconds) * (1.0 - static_cast<double>(i) / count);
            samples.push_back(static_cast<std::int16_t>(level * rise * fall * std::sin(2 * 3.14159265358979 * tone.hz * t)));
        }
    }
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    std::vector<std::uint8_t> wave(44 + dataBytes);
    auto* at = wave.data();
    const auto put = [&at](const void* value, std::size_t size) { std::memcpy(at, value, size); at += size; };
    const auto u32 = [&put](std::uint32_t value) { put(&value, 4); };
    const auto u16 = [&put](std::uint16_t value) { put(&value, 2); };
    put("RIFF", 4); u32(36 + dataBytes); put("WAVE", 4);
    put("fmt ", 4); u32(16); u16(1); u16(1); u32(Rate); u32(Rate * 2); u16(2); u16(16);
    put("data", 4); u32(dataBytes); put(samples.data(), dataBytes);
    return wave;
}

} }
