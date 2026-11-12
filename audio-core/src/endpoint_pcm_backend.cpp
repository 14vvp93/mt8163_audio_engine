#include "mt8163_audio_core/endpoint_pcm_backend.hpp"

namespace mt8163_audio_core {
namespace {
constexpr std::uint32_t state_value(PcmStreamState state) noexcept {
    return static_cast<std::uint32_t>(state);
}
}

EndpointPcmBackend::EndpointPcmBackend(const PcmEndpoint* endpoints,
                                       std::size_t endpoint_count,
                                       const PcmDeviceOps& ops) noexcept
    : ops_(ops) {
    if (endpoints == nullptr || endpoint_count == 0U ||
        endpoint_count > kMaxPcmEndpoints || ops_.open == nullptr ||
        ops_.configure == nullptr || ops_.start == nullptr ||
        ops_.write == nullptr || ops_.stop == nullptr || ops_.close == nullptr) {
        return;
    }

    for (std::size_t i = 0U; i < endpoint_count; ++i) {
        const auto& endpoint = endpoints[i];
        if (endpoint.enabled && !is_pcm_transport_route(endpoint.route)) return;
        if (endpoint.enabled) {
            for (std::size_t j = 0U; j < i; ++j) {
                if (endpoints[j].enabled && endpoints[j].route == endpoint.route) return;
            }
        }
        endpoints_[i] = endpoint;
    }
    endpoint_count_ = endpoint_count;
    valid_ = true;
}

const PcmEndpoint* EndpointPcmBackend::find_endpoint(RouteType route) const noexcept {
    for (std::size_t i = 0U; i < endpoint_count_; ++i) {
        if (endpoints_[i].enabled && endpoints_[i].route == route) return &endpoints_[i];
    }
    return nullptr;
}

EndpointPcmBackend::Slot* EndpointPcmBackend::find_slot(StreamToken token) noexcept {
    if (token == 0U) return nullptr;
    for (auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) != state_value(PcmStreamState::Closed) &&
            slot.token == token) return &slot;
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

PcmResult EndpointPcmBackend::open(RouteType route, StreamToken* token) noexcept {
    if (token == nullptr) return PcmResult::InvalidArgument;
    *token = 0U;
    if (!valid_) return PcmResult::InvalidState;
    const PcmEndpoint* endpoint = find_endpoint(route);
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
                                       endpoint->device_index, endpoint->open_flags,
                                       &native_handle);
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
    free_slot->token = allocated;
    free_slot->native_handle = native_handle;
    free_slot->config = PcmConfig{};
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
    const PcmResult result = ops_.start(ops_.context, slot->native_handle);
    slot->state.store(state_value(result == PcmResult::Ok ? PcmStreamState::Started
                                                          : PcmStreamState::Failed),
                      std::memory_order_release);
    return result;
}

PcmResult EndpointPcmBackend::write(StreamToken token,
                                    const std::int16_t* interleaved,
                                    std::size_t frame_count) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) != state_value(PcmStreamState::Started)) {
        return PcmResult::InvalidState;
    }
    if (interleaved == nullptr || frame_count == 0U) return PcmResult::InvalidArgument;
    if (frame_count > kMaxEndpointPcmWriteFrames) return PcmResult::BufferTooLarge;

    const PcmResult result = ops_.write(ops_.context, slot->native_handle,
                                        interleaved, frame_count);
    if (result == PcmResult::Xrun || result == PcmResult::DeviceLost) {
        slot->state.store(state_value(PcmStreamState::Failed), std::memory_order_release);
    }
    return result;
}

PcmResult EndpointPcmBackend::stop(StreamToken token) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return PcmResult::NotOpen;
    const auto state = static_cast<PcmStreamState>(slot->state.load(std::memory_order_acquire));
    if (state == PcmStreamState::Stopped) return PcmResult::Ok;
    if (state != PcmStreamState::Started) return PcmResult::InvalidState;
    const PcmResult result = ops_.stop(ops_.context, slot->native_handle);
    slot->state.store(state_value(result == PcmResult::Ok ? PcmStreamState::Stopped
                                                          : PcmStreamState::Failed),
                      std::memory_order_release);
    return result;
}

PcmResult EndpointPcmBackend::close(StreamToken token) noexcept {
    Slot* slot = find_slot(token);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) == state_value(PcmStreamState::Started)) {
        return PcmResult::InvalidState;
    }
    const PcmResult result = ops_.close(ops_.context, slot->native_handle);
    if (result == PcmResult::Ok) {
        slot->token = 0U;
        slot->native_handle = 0U;
        slot->config = PcmConfig{};
        slot->state.store(state_value(PcmStreamState::Closed), std::memory_order_release);
    } else {
        slot->state.store(state_value(PcmStreamState::Failed), std::memory_order_release);
    }
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
