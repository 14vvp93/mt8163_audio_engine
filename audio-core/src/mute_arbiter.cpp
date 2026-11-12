#include "mt8163_audio_core/mute_arbiter.hpp"

namespace mt8163_audio_core {

MuteSnapshot::MuteSnapshot(
    std::uint32_t master_owners,
    const std::array<std::uint32_t, kSourceCount>& source_owners) noexcept
    : master_owners_(master_owners), source_owners_(source_owners) {}

bool MuteSnapshot::master_muted() const noexcept {
    return master_owners_ != 0U;
}

bool MuteSnapshot::source_muted(SourceId source) const noexcept {
    const auto index = static_cast<std::size_t>(source);
    if (source == SourceId::None || index >= kSourceCount) return false;
    return master_muted() || source_owners_[index] != 0U;
}

std::uint32_t MuteSnapshot::master_owner_mask() const noexcept {
    return master_owners_;
}

std::uint32_t MuteSnapshot::source_owner_mask(SourceId source) const noexcept {
    const auto index = static_cast<std::size_t>(source);
    return source != SourceId::None && index < kSourceCount
        ? source_owners_[index] : 0U;
}

bool MuteArbiter::valid_owner(MuteOwner owner) noexcept {
    return static_cast<std::size_t>(owner) < kMuteOwnerCount;
}

bool MuteArbiter::valid_source(SourceId source) noexcept {
    return source != SourceId::None &&
           static_cast<std::size_t>(source) < kSourceCount;
}

std::uint32_t MuteArbiter::owner_bit(MuteOwner owner) noexcept {
    return std::uint32_t{1U} << static_cast<std::uint8_t>(owner);
}

bool MuteArbiter::set_master(MuteOwner owner, bool muted) noexcept {
    if (!valid_owner(owner)) return false;
    const std::uint32_t bit = owner_bit(owner);
    if (muted) {
        master_owners_ |= bit;
    } else {
        master_owners_ &= ~bit;
    }
    return true;
}

bool MuteArbiter::set_source(SourceId source, MuteOwner owner,
                             bool muted) noexcept {
    if (!valid_source(source) || !valid_owner(owner)) return false;
    const std::uint32_t bit = owner_bit(owner);
    auto& owners = source_owners_[static_cast<std::size_t>(source)];
    if (muted) {
        owners |= bit;
    } else {
        owners &= ~bit;
    }
    return true;
}

void MuteArbiter::clear_owner(MuteOwner owner) noexcept {
    if (!valid_owner(owner)) return;
    const std::uint32_t keep = ~owner_bit(owner);
    master_owners_ &= keep;
    for (auto& owners : source_owners_) owners &= keep;
}

bool MuteArbiter::master_muted() const noexcept {
    return master_owners_ != 0U;
}

bool MuteArbiter::source_muted(SourceId source) const noexcept {
    if (!valid_source(source)) return false;
    return master_muted() || source_locally_muted(source);
}

bool MuteArbiter::source_locally_muted(SourceId source) const noexcept {
    return valid_source(source) &&
           source_owners_[static_cast<std::size_t>(source)] != 0U;
}

std::uint32_t MuteArbiter::master_owner_mask() const noexcept {
    return master_owners_;
}

std::uint32_t MuteArbiter::source_owner_mask(SourceId source) const noexcept {
    return valid_source(source)
        ? source_owners_[static_cast<std::size_t>(source)] : 0U;
}

MuteSnapshot MuteArbiter::snapshot() const noexcept {
    return MuteSnapshot{master_owners_, source_owners_};
}

}  // namespace mt8163_audio_core
