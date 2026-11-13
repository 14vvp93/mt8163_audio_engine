#include "mt8163_audio_core/primary_hal_stream_adapter.hpp"

namespace mt8163_audio_core {

PrimaryHalStreamAdapter::PrimaryHalStreamAdapter(PcmBackend& backend) noexcept
    : streams_(backend) {}

PcmResult PrimaryHalStreamAdapter::open(
    std::uint32_t handle, SourceId source, RouteType route,
    PcmDirection direction, SessionToken* out_token) noexcept {
    if (out_token == nullptr) return PcmResult::InvalidArgument;
    *out_token = SessionToken{};
    if (handle == 0U) return PcmResult::InvalidArgument;
    if (validate_source_route(source, route) != RouteValidation::Supported ||
        !is_valid_pcm_route_direction(route, direction)) {
        return PcmResult::UnsupportedRoute;
    }

    for (const auto& record : audio_core_.sessions().records()) {
        if (record.state != SessionState::Free &&
            record.token.handle == handle) return PcmResult::InvalidArgument;
    }

    SessionToken session{};
    if (!audio_core_.open_session(handle, source, route, &session)) {
        return PcmResult::CapacityExceeded;
    }

    const PcmResult result = streams_.open(session, source, route, direction);
    if (result != PcmResult::Ok) {
        (void)audio_core_.close_session(session);
        return result;
    }
    *out_token = session;
    return PcmResult::Ok;
}

PcmResult PrimaryHalStreamAdapter::configure(
    SessionToken session, const PcmConfig& config) noexcept {
    return streams_.configure(session, config);
}

PcmResult PrimaryHalStreamAdapter::start(SessionToken session) noexcept {
    const PcmResult result = streams_.start(session);
    if (result != PcmResult::Ok) return result;
    if (!audio_core_.transition_session(session, SessionState::Active)) {
        (void)streams_.stop(session);
        return PcmResult::InvalidState;
    }
    return PcmResult::Ok;
}

PcmResult PrimaryHalStreamAdapter::write(
    SessionToken session, const std::int16_t* samples,
    std::size_t frames) noexcept {
    return streams_.write(session, samples, frames);
}

PcmResult PrimaryHalStreamAdapter::read(
    SessionToken session, std::int16_t* samples,
    std::size_t frames) noexcept {
    return streams_.read(session, samples, frames);
}

PcmTransferResult PrimaryHalStreamAdapter::write_transfer(
    SessionToken session, const std::int16_t* samples,
    std::size_t frames) noexcept {
    return streams_.write_transfer(session, samples, frames);
}

PcmTransferResult PrimaryHalStreamAdapter::read_transfer(
    SessionToken session, std::int16_t* samples,
    std::size_t frames) noexcept {
    return streams_.read_transfer(session, samples, frames);
}

PcmResult PrimaryHalStreamAdapter::stop(SessionToken session) noexcept {
    PcmStreamRecord record{};
    if (!streams_.get_record(session, &record)) return PcmResult::NotOpen;

    const SessionRecord* active_session = audio_core_.sessions().find(session);
    const bool was_active = active_session != nullptr &&
                            active_session->state == SessionState::Active;

    const PcmResult result = streams_.stop(session);
    if (result != PcmResult::Ok || !was_active) return result;

    return audio_core_.transition_session(session, SessionState::Suspended)
        ? PcmResult::Ok : PcmResult::InvalidState;
}

PcmResult PrimaryHalStreamAdapter::close(SessionToken session) noexcept {
    PcmStreamRecord record{};
    if (!streams_.get_record(session, &record)) return PcmResult::NotOpen;

    if (record.state == PcmStreamState::Started) {
        const PcmResult stop_result = stop(session);
        if (stop_result != PcmResult::Ok) return stop_result;
    } else if (record.state == PcmStreamState::Failed &&
               record.backend_stop_required) {
        const PcmResult stop_result = streams_.stop(session);
        if (stop_result != PcmResult::Ok) return stop_result;
    }

    const PcmResult result = streams_.close(session);
    if (result != PcmResult::Ok) return result;
    return audio_core_.close_session(session)
        ? PcmResult::Ok : PcmResult::InvalidState;
}

}  // namespace mt8163_audio_core
