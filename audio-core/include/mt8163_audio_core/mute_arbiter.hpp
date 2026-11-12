#pragma once

#include "mt8163_audio_core/types.hpp"

#include <array>
#include <cstdint>

namespace mt8163_audio_core {

class MuteSnapshot {
public:
    MuteSnapshot() noexcept = default;
    bool master_muted() const noexcept;
    bool source_muted(SourceId source) const noexcept;
    std::uint32_t master_owner_mask() const noexcept;
    std::uint32_t source_owner_mask(SourceId source) const noexcept;

private:
    friend class MuteArbiter;
    MuteSnapshot(std::uint32_t master_owners,
                 const std::array<std::uint32_t, kSourceCount>& source_owners)
        noexcept;

    std::uint32_t master_owners_{0U};
    std::array<std::uint32_t, kSourceCount> source_owners_{};
};

// Fixed-size, allocation-free owner-based mute state. Control-plane only:
// one serialized control context mutates/reads it, then publishes snapshots.
// Master and per-source reasons are independent domains. Clearing one owner
// never clears another owner's mute request.
class MuteArbiter {
public:
    bool set_master(MuteOwner owner, bool muted) noexcept;
    bool set_source(SourceId source, MuteOwner owner, bool muted) noexcept;

    void clear_owner(MuteOwner owner) noexcept;

    bool master_muted() const noexcept;
    bool source_muted(SourceId source) const noexcept;
    bool source_locally_muted(SourceId source) const noexcept;

    std::uint32_t master_owner_mask() const noexcept;
    std::uint32_t source_owner_mask(SourceId source) const noexcept;
    MuteSnapshot snapshot() const noexcept;

private:
    static bool valid_owner(MuteOwner owner) noexcept;
    static bool valid_source(SourceId source) noexcept;
    static std::uint32_t owner_bit(MuteOwner owner) noexcept;

    std::uint32_t master_owners_{0U};
    std::array<std::uint32_t, kSourceCount> source_owners_{};
};

}  // namespace mt8163_audio_core
