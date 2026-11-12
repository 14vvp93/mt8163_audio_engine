#include "mt8163_audio_core/pcm_stream_broker.hpp"

namespace mt8163_audio_core {

PcmStreamBroker::Slot* PcmStreamBroker::find_control(std::uint32_t handle) noexcept {
    if (handle == 0U) return nullptr;
    for (auto& slot : streams_) {
        if (slot.handle.load(std::memory_order_acquire) == handle) return &slot;
    }
    return nullptr;
}

const PcmStreamBroker::Slot* PcmStreamBroker::find_control(
    std::uint32_t handle) const noexcept {
    if (handle == 0U) return nullptr;
    for (const auto& slot : streams_) {
        if (slot.handle.load(std::memory_order_acquire) == handle) return &slot;
    }
    return nullptr;
}

PcmResult PcmStreamBroker::open(std::uint32_t handle, SourceId source,
                                RouteType route) noexcept {
    if (handle == 0U || find_control(handle) != nullptr) return PcmResult::InvalidArgument;
    if (validate_source_route(source, route) != RouteValidation::Supported ||
        !is_pcm_transport_route(route)) {
        return PcmResult::UnsupportedRoute;
    }

    Slot* free_slot = nullptr;
    for (auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) == static_cast<std::uint32_t>(PcmStreamState::Closed)) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) return PcmResult::CapacityExceeded;

    PcmBackend::StreamToken token = 0U;
    const PcmResult result = backend_.open(route, &token);
    if (result != PcmResult::Ok) return result;
    // Contract: a successful backend open must return a nonzero token. A
    // backend returning zero must have released any partially acquired resource.
    if (token == 0U) return PcmResult::BackendFailure;

    free_slot->source = source;
    free_slot->route = route;
    free_slot->config = PcmConfig{};
    free_slot->backend_token.store(token, std::memory_order_relaxed);
    free_slot->handle.store(handle, std::memory_order_release);
    free_slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Open), std::memory_order_release);
    return PcmResult::Ok;
}

PcmResult PcmStreamBroker::configure(std::uint32_t handle,
                                     const PcmConfig& config) noexcept {
    Slot* slot = find_control(handle);
    if (slot == nullptr) return PcmResult::NotOpen;
    const auto state = static_cast<PcmStreamState>(slot->state.load(std::memory_order_acquire));
    if (state != PcmStreamState::Open && state != PcmStreamState::Stopped) {
        return PcmResult::InvalidState;
    }
    if (!is_valid_pcm_config(config)) return PcmResult::InvalidArgument;

    const PcmResult result = backend_.configure(
        slot->backend_token.load(std::memory_order_relaxed), config);
    if (result == PcmResult::Ok) {
        slot->config = config;
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Configured), std::memory_order_release);
    } else {
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Failed), std::memory_order_release);
    }
    return result;
}

PcmResult PcmStreamBroker::start(std::uint32_t handle) noexcept {
    Slot* slot = find_control(handle);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) != static_cast<std::uint32_t>(PcmStreamState::Configured)) {
        return PcmResult::InvalidState;
    }

    const PcmResult result = backend_.start(
        slot->backend_token.load(std::memory_order_relaxed));
    slot->state.store(static_cast<std::uint32_t>(result == PcmResult::Ok
                          ? PcmStreamState::Started : PcmStreamState::Failed),
                      std::memory_order_release);
    return result;
}

PcmResult PcmStreamBroker::write(std::uint32_t handle,
                                 const std::int16_t* interleaved,
                                 std::size_t frame_count) noexcept {
    if (handle == 0U) return PcmResult::NotOpen;
    if (interleaved == nullptr || frame_count == 0U) return PcmResult::InvalidArgument;
    if (frame_count > kMaxPcmWriteFrames) return PcmResult::BufferTooLarge;

    for (const auto& slot : streams_) {
        if (slot.handle.load(std::memory_order_acquire) != handle) continue;
        if (slot.state.load(std::memory_order_acquire) != static_cast<std::uint32_t>(PcmStreamState::Started)) {
            return PcmResult::InvalidState;
        }
        const auto token = slot.backend_token.load(std::memory_order_relaxed);
        return backend_.write(token, interleaved, frame_count);
    }
    return PcmResult::NotOpen;
}

PcmResult PcmStreamBroker::stop(std::uint32_t handle) noexcept {
    Slot* slot = find_control(handle);
    if (slot == nullptr) return PcmResult::NotOpen;
    const PcmStreamState state = slot->state.load(std::memory_order_acquire);
    if (state == PcmStreamState::Stopped) return PcmResult::Ok;
    if (state != PcmStreamState::Started) return PcmResult::InvalidState;

    const PcmResult result = backend_.stop(
        slot->backend_token.load(std::memory_order_relaxed));
    slot->state.store(static_cast<std::uint32_t>(result == PcmResult::Ok
                          ? PcmStreamState::Stopped : PcmStreamState::Failed),
                      std::memory_order_release);
    return result;
}

PcmResult PcmStreamBroker::close(std::uint32_t handle) noexcept {
    Slot* slot = find_control(handle);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) == static_cast<std::uint32_t>(PcmStreamState::Started)) {
        return PcmResult::InvalidState;
    }

    const PcmResult result = backend_.close(
        slot->backend_token.load(std::memory_order_relaxed));
    if (result == PcmResult::Ok) {
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Closed), std::memory_order_release);
        slot->handle.store(0U, std::memory_order_release);
        slot->backend_token.store(0U, std::memory_order_relaxed);
        slot->source = SourceId::None;
        slot->route = RouteType::None;
        slot->config = PcmConfig{};
    } else {
        slot->state.store(PcmStreamState::Failed, std::memory_order_release);
    }
    return result;
}

bool PcmStreamBroker::get_record(std::uint32_t handle,
                                 PcmStreamRecord* out) const noexcept {
    if (out == nullptr) return false;
    const Slot* slot = find_control(handle);
    if (slot == nullptr) return false;
    out->handle = handle;
    out->backend_token = slot->backend_token.load(std::memory_order_relaxed);
    out->source = slot->source;
    out->route = slot->route;
    out->config = slot->config;
    out->state = static_cast<PcmStreamState>(slot->state.load(std::memory_order_acquire));
    return true;
}

std::size_t PcmStreamBroker::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) != static_cast<std::uint32_t>(PcmStreamState::Closed)) ++count;
    }
    return count;
}

}  // namespace mt8163_audio_core
