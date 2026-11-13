#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"

namespace mt8163_audio_core {

PrimaryHalStreamAdapter::PrimaryHalStreamAdapter(PcmBackend& backend) noexcept
    : streams_(backend) {}

PcmResult PrimaryHalStreamAdapter::open(std::uint32_t handle, SourceId source,
                                        RouteType route, PcmDirection direction) noexcept {
    if (handle == 0U) return PcmResult::InvalidArgument;
    if (validate_source_route(source, route) != RouteValidation::Supported ||
        !is_valid_pcm_route_direction(route, direction)) return PcmResult::UnsupportedRoute;
    if (audio_core_.sessions().find(handle) != nullptr) {
        return PcmResult::InvalidArgument;
    }
    if (!audio_core_.open_session(handle, source, route)) {
        return PcmResult::CapacityExceeded;
    }
    const PcmResult result = streams_.open(handle, source, route, direction);
    if (result != PcmResult::Ok) (void)audio_core_.close_session(handle);
    return result;
}

PcmResult PrimaryHalStreamAdapter::configure(std::uint32_t handle,
                                             const PcmConfig& config) noexcept {
    return streams_.configure(handle, config);
}

PcmResult PrimaryHalStreamAdapter::start(std::uint32_t handle) noexcept {
    const PcmResult result = streams_.start(handle);
    if (result != PcmResult::Ok) return result;
    if (!audio_core_.transition_session(handle, SessionState::Active)) {
        (void)streams_.stop(handle);
        return PcmResult::InvalidState;
    }
    return PcmResult::Ok;
}

PcmResult PrimaryHalStreamAdapter::write(std::uint32_t handle,
                                         const std::int16_t* samples,
                                         std::size_t frames) noexcept {
    return streams_.write(handle, samples, frames);
}

PcmResult PrimaryHalStreamAdapter::read(std::uint32_t handle,
                                        std::int16_t* samples,
                                        std::size_t frames) noexcept {
    return streams_.read(handle, samples, frames);
}

PcmTransferResult PrimaryHalStreamAdapter::write_transfer(
    std::uint32_t handle, const std::int16_t* samples, std::size_t frames) noexcept {
    return streams_.write_transfer(handle, samples, frames);
}

PcmTransferResult PrimaryHalStreamAdapter::read_transfer(
    std::uint32_t handle, std::int16_t* samples, std::size_t frames) noexcept {
    return streams_.read_transfer(handle, samples, frames);
}

PcmResult PrimaryHalStreamAdapter::stop(std::uint32_t handle) noexcept {
    PcmStreamRecord record{};
    if (!streams_.get_record(handle, &record)) return PcmResult::NotOpen;

    const SessionRecord* session = audio_core_.sessions().find(handle);
    const bool session_was_active =
        session != nullptr && session->state == SessionState::Active;

    const PcmResult result = streams_.stop(handle);
    if (result != PcmResult::Ok) return result;
    if (!session_was_active) return PcmResult::Ok;

    return audio_core_.transition_session(handle, SessionState::Suspended)
        ? PcmResult::Ok : PcmResult::InvalidState;
}

PcmResult PrimaryHalStreamAdapter::close(std::uint32_t handle) noexcept {
    PcmStreamRecord record{};
    if (!streams_.get_record(handle, &record)) return PcmResult::NotOpen;
    if (record.state == PcmStreamState::Started) {
        const PcmResult stop_result = stop(handle);
        if (stop_result != PcmResult::Ok) return stop_result;
    } else if (record.state == PcmStreamState::Failed &&
               record.backend_stop_required) {
        // Після невдалого start сесія AudioCore ще не перейшла в Active.
        const PcmResult stop_result = streams_.stop(handle);
        if (stop_result != PcmResult::Ok) return stop_result;
    }
    const PcmResult result = streams_.close(handle);
    if (result != PcmResult::Ok) return result;
    return audio_core_.close_session(handle) ? PcmResult::Ok : PcmResult::InvalidState;
}

}  // namespace mt8163_audio_core
