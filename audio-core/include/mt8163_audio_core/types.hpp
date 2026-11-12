#pragma once

#include <cstddef>
#include <cstdint>

namespace mt8163_audio_core {

enum class SourceId : std::uint8_t {
    None = 0,
    LocalMedia,
    Navigation,
    Notification,
    BluetoothA2dp,
    BluetoothHfp,
    FmRadio,
    Aux,
    Aux2,
    Dab,
    Reverse,
    OnStar,
    Microphone,
    Count
};

enum class RouteType : std::uint8_t {
    None = 0,
    PrimaryPcm,
    PrimaryCapturePcm,
    A2dpPcm,
    HfpSco,
    FmDirectI2s,
    FmCapturePcm,
    Cd3313AnalogInput,
    HdmiPcm
};

enum class AudioMode : std::uint8_t {
    Normal = 0,
    InCall,
    InCommunication
};

// These are AudioCore ownership identities, not CD3313 reason numbers.
enum class MuteOwner : std::uint8_t {
    AccPowerState = 0,
    SilenceMode,
    SilenceTransition,
    BluetoothCallTransition,
    RadioRetune,
    RadioScan,
    RadioPacketMute,
    RadioStartupMute,
    RadioUserMute,
    UserRequest,
    SafetyInterlock,
    Count
};

constexpr std::size_t kSourceCount =
    static_cast<std::size_t>(SourceId::Count);
constexpr std::size_t kMuteOwnerCount =
    static_cast<std::size_t>(MuteOwner::Count);

static_assert(kMuteOwnerCount <= 32U,
              "MuteArbiter uses a 32-bit owner mask");

}  // namespace mt8163_audio_core
