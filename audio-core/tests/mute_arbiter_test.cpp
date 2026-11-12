#include "mt8163_audio_core/mute_arbiter.hpp"
#include "mt8163_audio_core/cd3313_compat.hpp"

#include <cstdlib>
#include <iostream>

using mt8163_audio_core::MuteArbiter;
using mt8163_audio_core::MuteOwner;
using mt8163_audio_core::SourceId;
using mt8163_audio_core::cd3313_master_reason_mask;
using mt8163_audio_core::cd3313_radio_input_reason_mask;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_independent_master_owners() {
    MuteArbiter mute;
    require(!mute.master_muted(), "master starts unmuted");
    require(mute.set_master(MuteOwner::AccPowerState, true),
            "ACC master mute accepted");
    require(mute.set_master(MuteOwner::BluetoothCallTransition, true),
            "Bluetooth master mute accepted");
    require(mute.master_muted(), "either owner keeps master muted");
    require(mute.set_master(MuteOwner::AccPowerState, false),
            "ACC master unmute accepted");
    require(mute.master_muted(), "clearing ACC does not clear Bluetooth mute");
    require(mute.set_master(MuteOwner::BluetoothCallTransition, false),
            "Bluetooth master unmute accepted");
    require(!mute.master_muted(), "master unmutes after all owners clear");
}

void test_source_scope_and_master_inheritance() {
    MuteArbiter mute;
    require(mute.set_source(SourceId::FmRadio, MuteOwner::RadioRetune, true),
            "radio retune mute accepted");
    require(mute.source_muted(SourceId::FmRadio), "FM is locally muted");
    require(!mute.source_muted(SourceId::LocalMedia),
            "radio mute does not mute local media");
    require(mute.set_master(MuteOwner::AccPowerState, true),
            "master ACC mute accepted");
    require(mute.source_muted(SourceId::LocalMedia),
            "master mute applies to every source");
    require(mute.set_master(MuteOwner::AccPowerState, false),
            "master ACC mute cleared");
    require(mute.source_muted(SourceId::FmRadio),
            "local FM mute survives master unmute");
}

void test_clear_owner_is_scoped() {
    MuteArbiter mute;
    mute.set_master(MuteOwner::SilenceMode, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioScan, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioPacketMute, true);
    mute.clear_owner(MuteOwner::RadioScan);
    require(mute.master_muted(), "clearing radio scan leaves master mute");
    require(mute.source_locally_muted(SourceId::FmRadio),
            "other radio mute owner remains active");
    require(mute.source_owner_mask(SourceId::FmRadio) != 0U,
            "remaining radio owner bit is retained");
}

void test_idempotence_and_validation() {
    MuteArbiter mute;
    require(mute.set_master(MuteOwner::AccPowerState, true),
            "first ACC set accepted");
    const auto once = mute.master_owner_mask();
    require(mute.set_master(MuteOwner::AccPowerState, true),
            "duplicate ACC set accepted");
    require(mute.master_owner_mask() == once,
            "duplicate set is idempotent, not a reference count");
    require(!mute.set_source(SourceId::None, MuteOwner::UserRequest, true),
            "NONE cannot own a source mute");
    require(!mute.set_master(MuteOwner::Count, true),
            "invalid owner is rejected");
    require(!mute.source_muted(SourceId::Count),
            "invalid source is not reported muted");
}


void test_snapshot_is_immutable_value() {
    MuteArbiter mute;
    mute.set_master(MuteOwner::AccPowerState, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioRetune, true);
    const auto published = mute.snapshot();
    mute.set_master(MuteOwner::AccPowerState, false);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioRetune, false);
    const auto current = mute.snapshot();
    require(published.master_muted(), "published snapshot retains master mute");
    require(published.source_muted(SourceId::FmRadio),
            "published snapshot retains local source mute");
    require(!current.source_muted(SourceId::LocalMedia),
            "new snapshot reflects cleared master mute");
    require(!current.source_muted(SourceId::FmRadio),
            "new snapshot reflects cleared source mute");
}

void test_cd3313_legacy_reason_mapping() {
    MuteArbiter mute;
    mute.set_master(MuteOwner::AccPowerState, true);
    mute.set_master(MuteOwner::SilenceMode, true);
    mute.set_master(MuteOwner::SilenceTransition, true);
    mute.set_master(MuteOwner::BluetoothCallTransition, true);
    const std::uint32_t expected_master =
        (std::uint32_t{1U} << 2U) |
        (std::uint32_t{1U} << 1U) |
        (std::uint32_t{1U} << 3U) |
        (std::uint32_t{1U} << 24U);
    require(cd3313_master_reason_mask(mute.master_owner_mask()) == expected_master,
            "master owners map to recovered CD3313 reason bits");

    mute.set_source(SourceId::FmRadio, MuteOwner::RadioScan, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioPacketMute, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioRetune, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioStartupMute, true);
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioUserMute, true);
    auto radio_mask = mute.source_owner_mask(SourceId::FmRadio);
    const std::uint32_t expected_radio =
        (std::uint32_t{1U} << 1U) |
        (std::uint32_t{1U} << 2U) |
        (std::uint32_t{1U} << 4U) |
        (std::uint32_t{1U} << 16U);
    require(cd3313_radio_input_reason_mask(radio_mask) == expected_radio,
            "radio owners map to input-path reason bits");
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioScan, false);
    radio_mask = mute.source_owner_mask(SourceId::FmRadio);
    require((cd3313_radio_input_reason_mask(radio_mask) &
             (std::uint32_t{1U} << 2U)) != 0U,
            "shared legacy reason remains while another owner is active");
    mute.set_source(SourceId::FmRadio, MuteOwner::RadioPacketMute, false);
    radio_mask = mute.source_owner_mask(SourceId::FmRadio);
    require((cd3313_radio_input_reason_mask(radio_mask) &
             (std::uint32_t{1U} << 2U)) == 0U,
            "shared legacy reason clears after every mapped owner clears");
}

}  // namespace

int main() {
    test_independent_master_owners();
    test_source_scope_and_master_inheritance();
    test_clear_owner_is_scoped();
    test_idempotence_and_validation();
    test_cd3313_legacy_reason_mapping();
    test_snapshot_is_immutable_value();
    std::cout << "AudioCore mute arbiter tests passed\n";
    return 0;
}
