#include "mt8163_audio_core/endpoint_pcm_backend.hpp"

namespace mt8163_audio_core {
namespace {
constexpr std::uint32_t state_value(PcmStreamState state) noexcept {
    return static_cast<std::uint32_t>(state);
}

PcmTransferResult invalid_transfer(PcmResult result) noexcept {
    return {result, 0U};
}
}

EndpointPcmBackend::EndpointPcmBackend(const PcmEndpoint* endpoints,
                                       std::size_t endpoint_count,
                                       const PcmDeviceOps& ops) noexcept
    : ops_(ops) {
    if (endpoints == nullptr || endpoint_count == 0U ||
        endpoint_count > kMaxPcmEndpoints || ops_.open == nullptr ||
        ops_.configure == nullptr || ops_.start == nullptr ||
        ops_.stop == nullptr || ops_.close == nullptr) return;

    for (std::size_t i = 0U; i < endpoint_count; ++i) {
        const auto& endpoint = endpoints[i];
        if (endpoint.enabled &&
            !is_valid_pcm_route_direction(endpoint.route, endpoint.direction)) return;
        if (endpoint.enabled &&
            ((endpoint.direction == PcmDirection::Playback &&
              ops_.write == nullptr && ops_.write_transfer == nullptr) ||
             (endpoint.direction == PcmDirection::Capture &&
              ops_.read == nullptr && ops_.read_transfer == nullptr))) return;
        if (endpoint.enabled) {
            for (std::size_t j = 0U; j < i; ++j) {
                if (endpoints[j].enabled && endpoints[j].route == endpoint.route &&
                    endpoints[j].direction == endpoint.direction) return;
            }
        }
        endpoints_[i] = endpoint;
    }
    endpoint_count_ = endpoint_count;
    valid_ = true;
}

const PcmEndpoint* EndpointPcmBackend::find_endpoint(
    RouteType route, PcmDirection direction) const noexcept {
    for (std::size_t i = 0U; i < endpoint_count_; ++i) {
        if (endpoints_[i].enabled && endpoints_[i].route == route &&
            endpoints_[i].direction == direction) return &endpoints_[i];
    }
    return nullptr;
}

EndpointPcmBackend::Slot* EndpointPcmBackend::find_slot(StreamToken token) noexcept {
    if (token == 0U) return nullptr;
    for (auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) != state_value(PcmStreamState::Closed) &&
            slot.token.load(std::memory_order_acquire) == token) return &slot;
    }
    return nullptr;
}

const EndpointPcmBackend::Slot* EndpointPcmBackend::find_slot(
    StreamToken token) const noexcept {
    if (token == 0U) return nullptr;
    for (const auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) != state_value(PcmStreamState::Closed) &&
            slot.token == token) return &slot;
    }
    return nullptr;
}

EndpointPcmBackend::StreamToken EndpointPcmBackend::allocate_token() noexcept {
    for (std::size_t attempt = 0U; attempt <= streams_.size(); ++attempt) {
        StreamToken candidate = next_token_++;
        if (next_token_ == 0U) next_token_ = 1U;
        if (candidate != 0U && find_slot(candidate) == nullptr) return candidate;
    }
    return 0U;
}

PcmResult EndpointPcmBackend::open(RouteType route, PcmDirection direction,
                                   StreamToken* token) noexcept {
    if (token == nullptr) return PcmResult::InvalidArgument;
    *token = 0U;
    if (!valid_) return PcmResult::InvalidState;
    if (!is_valid_pcm_route_direction(route, direction)) return PcmResult::UnsupportedRoute;
    const PcmEndpoint* endpoint = find_endpoint(route, direction);
    if (endpoint == nullptr) return PcmResult::UnsupportedRoute;

    Slot* free_slot = nullptr;
    for (auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) == state_value(PcmStreamState::Closed)) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) return PcmResult::CapacityExceeded;

    std::uint32_t native_handle = 0U;
    const PcmResult result = ops_.open(ops_.context, endpoint->card_index,
                                       endpoint->device_index, direction,
                                       endpoint->open_flags, &native_handle);
    if (result != PcmResult::Ok) {
        if (native_handle != 0U) (void)ops_.close(ops_.context, native_handle);
        return result;
    }
    if (native_handle == 0U) return PcmResult::BackendFailure;

    const StreamToken allocated = allocate_token();
    if (allocated == 0U) {
        (void)ops_.close(ops_.context, native_handle);
        return PcmResult::CapacityExceeded;
    }
    free_slot->token.store(allocated, std::memory_order_release);
    free_slot->native_handle = native_handle;
    free_slot->config = PcmConfig{};
    free_slot->direction = direction;
    free_slot->stop_required = false;
    free_slot->transfer_busy.clear(std::memory_order_relaxed);
    free_slot->state.store(state_value(PcmStreamState::Open), std::memory_order_release);
    *token = allocated;
    return PcmResult::Ok;
}

PcmResult EndpointPcmBackend::configure(StreamToken token,
                                        const PcmConfig& config) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return PcmResult::NotOpen;
    const auto state = static_cast<PcmStreamState>(slot->state.load(std::memory_order_acquire));
    if (state != PcmStreamState::Open && state != PcmStreamState::Stopped) {
        return PcmResult::InvalidState;
    }
    if (!is_valid_pcm_config(config)) return PcmResult::InvalidArgument;
    const PcmResult result = ops_.configure(ops_.context, slot->native_handle, config);
    if (result == PcmResult::Ok) {
        slot->config = config;
        slot->state.store(state_value(PcmStreamState::Configured), std::memory_order_release);
    } else {
        slot->state.store(state_value(PcmStreamState::Failed), std::memory_order_release);
    }
    return result;
}

PcmResult EndpointPcmBackend::start(StreamToken token) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) != state_value(PcmStreamState::Configured)) {
        return PcmResult::InvalidState;
    }
    slot->stop_required = true;
    const PcmResult result = ops_.start(ops_.context, slot->native_handle);
    slot->state.store(state_value(result == PcmResult::Ok ? PcmStreamState::Started
                                                          : PcmStreamState::Failed),
                      std::memory_order_release);
    return result;
}

PcmTransferResult EndpointPcmBackend::transfer(StreamToken token, std::int16_t* buffer,
                                               std::size_t frame_count,
                                               PcmDirection direction) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return invalid_transfer(PcmResult::NotOpen);
    if (slot->state.load(std::memory_order_acquire) != state_value(PcmStreamState::Started)) {
        return invalid_transfer(PcmResult::InvalidState);
    }
    if (buffer == nullptr || frame_count == 0U) {
        return invalid_transfer(PcmResult::InvalidArgument);
    }
    if (frame_count > kMaxEndpointPcmTransferFrames) {
        return invalid_transfer(PcmResult::BufferTooLarge);
    }
    if (slot->transfer_busy.test_and_set(std::memory_order_acquire)) {
        return invalid_transfer(PcmResult::WouldBlock);
    }

    if (slot->token.load(std::memory_order_acquire) != token) {
        slot->transfer_busy.clear(std::memory_order_release);
        return invalid_transfer(PcmResult::NotOpen);
    }
    if (slot->direction != direction ||
        slot->state.load(std::memory_order_acquire) != state_value(PcmStreamState::Started)) {
        slot->transfer_busy.clear(std::memory_order_release);
        return invalid_transfer(PcmResult::InvalidState);
    }

    PcmTransferResult result{};
    if (direction == PcmDirection::Playback) {
        result = ops_.write_transfer != nullptr
            ? ops_.write_transfer(ops_.context, slot->native_handle, buffer, frame_count)
            : PcmTransferResult{ops_.write(ops_.context, slot->native_handle, buffer, frame_count),
                                0U};
    } else {
        result = ops_.read_transfer != nullptr
            ? ops_.read_transfer(ops_.context, slot->native_handle, buffer, frame_count)
            : PcmTransferResult{ops_.read(ops_.context, slot->native_handle, buffer, frame_count),
                                0U};
    }

    if (ops_.write_transfer == nullptr && direction == PcmDirection::Playback &&
        result.result == PcmResult::Ok) result.frames_transferred = frame_count;
    if (ops_.read_transfer == nullptr && direction == PcmDirection::Capture &&
        result.result == PcmResult::Ok) result.frames_transferred = frame_count;

    if (result.frames_transferred > frame_count ||
        (result.result == PcmResult::Ok && result.frames_transferred == 0U)) {
        result = invalid_transfer(PcmResult::BackendFailure);
        slot->state.store(state_value(PcmStreamState::Failed), std::memory_order_release);
    } else if (result.result == PcmResult::Xrun || result.result == PcmResult::DeviceLost) {
        slot->state.store(state_value(PcmStreamState::Failed), std::memory_order_release);
    }
    slot->transfer_busy.clear(std::memory_order_release);
    return result;
}

PcmResult EndpointPcmBackend::write(StreamToken token,
                                    const std::int16_t* interleaved,
                                    std::size_t frame_count) noexcept {
    const PcmTransferResult result = write_transfer(token, interleaved, frame_count);
    if (result.result != PcmResult::Ok) return result.result;
    return result.frames_transferred == frame_count ? PcmResult::Ok : PcmResult::BackendFailure;
}

PcmResult EndpointPcmBackend::read(StreamToken token,
                                   std::int16_t* interleaved,
                                   std::size_t frame_count) noexcept {
    const PcmTransferResult result = read_transfer(token, interleaved, frame_count);
    if (result.result != PcmResult::Ok) return result.result;
    return result.frames_transferred == frame_count ? PcmResult::Ok : PcmResult::BackendFailure;
}

PcmTransferResult EndpointPcmBackend::write_transfer(
    StreamToken token, const std::int16_t* interleaved,
    std::size_t frame_count) noexcept {
    return transfer(token, const_cast<std::int16_t*>(interleaved), frame_count,
                    PcmDirection::Playback);
}

PcmTransferResult EndpointPcmBackend::read_transfer(
    StreamToken token, std::int16_t* interleaved,
    std::size_t frame_count) noexcept {
    return transfer(token, interleaved, frame_count, PcmDirection::Capture);
}

PcmResult EndpointPcmBackend::stop(StreamToken token) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return PcmResult::NotOpen;
    const auto state = static_cast<PcmStreamState>(slot->state.load(std::memory_order_acquire));
    if (state == PcmStreamState::Stopped) return PcmResult::Ok;
    if ((state != PcmStreamState::Started && state != PcmStreamState::Failed) ||
        !slot->stop_required) {
        return PcmResult::InvalidState;
    }
    // Close callback admission before attempting to acquire the in-flight transfer gate.
    slot->state.store(state_value(PcmStreamState::Failed), std::memory_order_release);
    if (slot->transfer_busy.test_and_set(std::memory_order_acquire)) return PcmResult::WouldBlock;
    if (slot->token.load(std::memory_order_acquire) != token) {
        slot->transfer_busy.clear(std::memory_order_release);
        return PcmResult::NotOpen;
    }
    const auto locked_state = static_cast<PcmStreamState>(
        slot->state.load(std::memory_order_acquire));
    if (locked_state == PcmStreamState::Stopped) {
        slot->transfer_busy.clear(std::memory_order_release);
        return PcmResult::Ok;
    }
    if ((locked_state != PcmStreamState::Started && locked_state != PcmStreamState::Failed) ||
        !slot->stop_required) {
        slot->transfer_busy.clear(std::memory_order_release);
        return PcmResult::InvalidState;
    }
    const PcmResult result = ops_.stop(ops_.context, slot->native_handle);
    if (result == PcmResult::Ok) slot->stop_required = false;
    slot->state.store(state_value(result == PcmResult::Ok ? PcmStreamState::Stopped
                                                          : PcmStreamState::Failed),
                      std::memory_order_release);
    slot->transfer_busy.clear(std::memory_order_release);
    return result;
}

PcmResult EndpointPcmBackend::close(StreamToken token) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) == state_value(PcmStreamState::Started) ||
        slot->stop_required) {
        return PcmResult::InvalidState;
    }
    if (slot->transfer_busy.test_and_set(std::memory_order_acquire)) return PcmResult::WouldBlock;
    if (slot->token.load(std::memory_order_acquire) != token) {
        slot->transfer_busy.clear(std::memory_order_release);
        return PcmResult::NotOpen;
    }
    if (slot->state.load(std::memory_order_acquire) == state_value(PcmStreamState::Started) ||
        slot->stop_required) {
        slot->transfer_busy.clear(std::memory_order_release);
        return PcmResult::InvalidState;
    }
    const PcmResult result = ops_.close(ops_.context, slot->native_handle);
    if (result == PcmResult::Ok) {
        slot->token.store(0U, std::memory_order_release);
        slot->native_handle = 0U;
        slot->config = PcmConfig{};
        slot->direction = PcmDirection::Playback;
        slot->stop_required = false;
        slot->state.store(state_value(PcmStreamState::Closed), std::memory_order_release);
    } else {
        slot->state.store(state_value(PcmStreamState::Failed), std::memory_order_release);
    }
    slot->transfer_busy.clear(std::memory_order_release);
    return result;
}

std::size_t EndpointPcmBackend::active_streams() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) != state_value(PcmStreamState::Closed)) {
            ++count;
        }
    }
    return count;
}

}  // namespace mt8163_audio_core
