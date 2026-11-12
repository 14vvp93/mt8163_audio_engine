#include "mt8163_audio_core/pcm_stream_broker.hpp"

namespace mt8163_audio_core {

PcmStreamRecord* PcmStreamBroker::find_mutable(std::uint32_t handle) noexcept {
    if (handle == 0U) return nullptr;
    for (auto& stream : streams_) {
        if (stream.state != PcmStreamState::Closed && stream.handle == handle) {
            return &stream;
        }
    }
    return nullptr;
}

const PcmStreamRecord* PcmStreamBroker::find(std::uint32_t handle) const noexcept {
    if (handle == 0U) return nullptr;
    for (const auto& stream : streams_) {
        if (stream.state != PcmStreamState::Closed && stream.handle == handle) {
            return &stream;
        }
    }
    return nullptr;
}

PcmResult PcmStreamBroker::open(std::uint32_t handle, SourceId source,
                                RouteType route) noexcept {
    if (handle == 0U || find(handle) != nullptr) return PcmResult::InvalidArgument;
    if (validate_source_route(source, route) != RouteValidation::Supported ||
        !is_pcm_transport_route(route)) {
        return PcmResult::UnsupportedRoute;
    }

    PcmStreamRecord* free_slot = nullptr;
    for (auto& stream : streams_) {
        if (stream.state == PcmStreamState::Closed) {
            free_slot = &stream;
            break;
        }
    }
    if (free_slot == nullptr) return PcmResult::CapacityExceeded;

    PcmBackend::StreamToken token = 0U;
    const PcmResult result = backend_.open(route, &token);
    if (result != PcmResult::Ok) return result;
    if (token == 0U) return PcmResult::BackendFailure;

    *free_slot = PcmStreamRecord{};
    free_slot->handle = handle;
    free_slot->backend_token = token;
    free_slot->source = source;
    free_slot->route = route;
    free_slot->state = PcmStreamState::Open;
    return PcmResult::Ok;
}

PcmResult PcmStreamBroker::configure(std::uint32_t handle,
                                     const PcmConfig& config) noexcept {
    PcmStreamRecord* stream = find_mutable(handle);
    if (stream == nullptr) return PcmResult::NotOpen;
    if (stream->state != PcmStreamState::Open &&
        stream->state != PcmStreamState::Stopped) {
        return PcmResult::InvalidState;
    }
    if (!is_valid_pcm_config(config)) return PcmResult::InvalidArgument;

    const PcmResult result = backend_.configure(stream->backend_token, config);
    if (result == PcmResult::Ok) {
        stream->config = config;
        stream->state = PcmStreamState::Configured;
    } else {
        stream->state = PcmStreamState::Failed;
    }
    return result;
}

PcmResult PcmStreamBroker::start(std::uint32_t handle) noexcept {
    PcmStreamRecord* stream = find_mutable(handle);
    if (stream == nullptr) return PcmResult::NotOpen;
    if (stream->state != PcmStreamState::Configured) return PcmResult::InvalidState;

    const PcmResult result = backend_.start(stream->backend_token);
    stream->state = result == PcmResult::Ok ? PcmStreamState::Started
                                            : PcmStreamState::Failed;
    return result;
}

PcmResult PcmStreamBroker::write(std::uint32_t handle,
                                 const std::int16_t* interleaved,
                                 std::size_t frame_count) noexcept {
    const PcmStreamRecord* stream = find(handle);
    if (stream == nullptr) return PcmResult::NotOpen;
    if (stream->state != PcmStreamState::Started) return PcmResult::InvalidState;
    if (interleaved == nullptr || frame_count == 0U) {
        return PcmResult::InvalidArgument;
    }
    if (frame_count > kMaxPcmWriteFrames) return PcmResult::BufferTooLarge;
    return backend_.write(stream->backend_token, interleaved, frame_count);
}

PcmResult PcmStreamBroker::stop(std::uint32_t handle) noexcept {
    PcmStreamRecord* stream = find_mutable(handle);
    if (stream == nullptr) return PcmResult::NotOpen;
    if (stream->state == PcmStreamState::Stopped) return PcmResult::Ok;
    if (stream->state != PcmStreamState::Started) return PcmResult::InvalidState;

    const PcmResult result = backend_.stop(stream->backend_token);
    stream->state = result == PcmResult::Ok ? PcmStreamState::Stopped
                                            : PcmStreamState::Failed;
    return result;
}

PcmResult PcmStreamBroker::close(std::uint32_t handle) noexcept {
    PcmStreamRecord* stream = find_mutable(handle);
    if (stream == nullptr) return PcmResult::NotOpen;
    if (stream->state == PcmStreamState::Started) return PcmResult::InvalidState;

    const PcmResult result = backend_.close(stream->backend_token);
    if (result == PcmResult::Ok) *stream = PcmStreamRecord{};
    else stream->state = PcmStreamState::Failed;
    return result;
}

std::size_t PcmStreamBroker::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& stream : streams_) {
        if (stream.state != PcmStreamState::Closed) ++count;
    }
    return count;
}

}  // namespace mt8163_audio_core
