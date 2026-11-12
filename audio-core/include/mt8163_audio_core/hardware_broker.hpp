#pragma once

#include "mt8163_audio_core/types.hpp"

#include <cstdint>

namespace mt8163_audio_core {

// Semantic commands only. This is deliberately not the CD3313 ioctl wire ABI.
// The exact transport framing remains blocked on the recovered native/kernel
// ABI discrepancy and must be implemented by a separately validated backend.
enum class HardwareCommandKind : std::uint8_t {
    SelectSource = 0,
    SetMasterMute,
    SetSourceMute
};

struct HardwareCommand {
    HardwareCommandKind kind{HardwareCommandKind::SelectSource};
    SourceId source{SourceId::None};
    MuteOwner owner{MuteOwner::AccPowerState};
    bool enabled{false};
};

enum class HardwareResult : std::uint8_t {
    Applied = 0,
    Unsupported,
    InvalidCommand,
    NoBackend,
    TransportError
};

// Implementations run only on the serialized AudioCore control context.
// They must never be called from a real-time PCM callback.
class HardwareBackend {
public:
    virtual ~HardwareBackend() = default;
    virtual HardwareResult execute(
        const HardwareCommand& command) noexcept = 0;
};

// Non-owning backend reference. The backend must outlive this broker.
// Single-control-thread only: no mutex, allocation, retry or hidden worker.
class HardwareCommandBroker {
public:
    void attach(HardwareBackend* backend) noexcept;
    HardwareResult dispatch(const HardwareCommand& command) noexcept;
    bool has_backend() const noexcept;

    static bool valid(const HardwareCommand& command) noexcept;

private:
    HardwareBackend* backend_{nullptr};
};

}  // namespace mt8163_audio_core
