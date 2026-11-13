#pragma once

#include <cstdint>
#include <mutex>

namespace audio_core {

enum class AudioSource : std::uint8_t {
    None,
    Music,
    BluetoothA2dp,
    FmRadio,
    Aux
};

enum class OutputRoute : std::uint8_t {
    None,
    Speaker,
    WiredHeadset,
    WiredHeadphones
};

enum class MuteReason : std::uint32_t {
    User = 1U << 0U,
    System = 1U << 1U,
    Call = 1U << 2U,
    Reverse = 1U << 3U,
    SourceTransition = 1U << 4U
};

struct AudioStateSnapshot {
    std::uint64_t generation{0};
    AudioSource source{AudioSource::None};
    OutputRoute route{OutputRoute::None};
    std::uint32_t mute_reasons{0};
    float master_volume{1.0F};
    bool dsp_enabled{false};

    bool muted() const noexcept { return mute_reasons != 0U; }
};

class AudioState final {
public:
    AudioState() = default;
    AudioState(const AudioState&) = delete;
    AudioState& operator=(const AudioState&) = delete;

    AudioStateSnapshot snapshot() const;

    bool set_source(AudioSource source);
    bool set_route(OutputRoute route);
    bool set_master_volume(float volume);
    bool set_mute(MuteReason reason, bool enabled);
    bool set_dsp_enabled(bool enabled);

private:
    static bool valid_source(AudioSource source) noexcept;
    static bool valid_route(OutputRoute route) noexcept;
    static bool valid_mute_reason(MuteReason reason) noexcept;
    bool advance_generation_locked() noexcept;

    mutable std::mutex mutex_;
    AudioStateSnapshot state_{};
};

} // namespace audio_core
