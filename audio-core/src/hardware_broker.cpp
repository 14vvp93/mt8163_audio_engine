#include "mt8163_audio_core/hardware_broker.hpp"

namespace mt8163_audio_core {
namespace {

bool valid_source(SourceId source) noexcept {
    return source > SourceId::None && source < SourceId::Count;
}

bool valid_owner(MuteOwner owner) noexcept {
    return owner < MuteOwner::Count;
}

}  // namespace

void HardwareCommandBroker::attach(HardwareBackend* backend) noexcept {
    backend_ = backend;
}

HardwareResult HardwareCommandBroker::dispatch(
    const HardwareCommand& command) noexcept {
    if (!valid(command)) return HardwareResult::InvalidCommand;
    if (backend_ == nullptr) return HardwareResult::NoBackend;
    return backend_->execute(command);
}

bool HardwareCommandBroker::has_backend() const noexcept {
    return backend_ != nullptr;
}

bool HardwareCommandBroker::valid(
    const HardwareCommand& command) noexcept {
    switch (command.kind) {
        case HardwareCommandKind::SelectSource:
            return valid_source(command.source);
        case HardwareCommandKind::SetMasterMute:
            return command.source == SourceId::None &&
                   valid_owner(command.owner);
        case HardwareCommandKind::SetSourceMute:
            return valid_source(command.source) &&
                   valid_owner(command.owner);
        default:
            return false;
    }
}

}  // namespace mt8163_audio_core
