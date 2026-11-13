#include "mt8163_audio_core/pcm_stream_broker.hpp"

namespace mt8163_audio_core {
namespace {
constexpr std::uint32_t kTransferGateOpen = 0x80000000U;
constexpr std::uint32_t kTransferCountMask = 0x7FFFFFFFU;

PcmTransferResult transfer_error(PcmResult result) noexcept {
    return {result, 0U};
}
}

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
                                RouteType route, PcmDirection direction) noexcept {
    if (handle == 0U || find_control(handle) != nullptr) return PcmResult::InvalidArgument;
    if (validate_source_route(source, route) != RouteValidation::Supported ||
        !is_valid_pcm_route_direction(route, direction)) {
        return PcmResult::UnsupportedRoute;
    }
    Slot* free_slot = nullptr;
    for (auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(PcmStreamState::Closed)) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) return PcmResult::CapacityExceeded;

    PcmBackend::StreamToken token = 0U;
    const PcmResult result = backend_.open(route, direction, &token);
    if (result != PcmResult::Ok) return result;
    if (token == 0U) return PcmResult::BackendFailure;

    free_slot->source = source;
    free_slot->route = route;
    free_slot->direction = direction;
    free_slot->config = PcmConfig{};
    free_slot->backend_stop_required = false;
    free_slot->transfer_gate.store(0U, std::memory_order_relaxed);
    free_slot->generation.fetch_add(1U, std::memory_order_relaxed);
    free_slot->backend_token.store(token, std::memory_order_relaxed);
    free_slot->handle.store(handle, std::memory_order_release);
    free_slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Open),
                           std::memory_order_release);
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
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Configured),
                          std::memory_order_release);
    } else {
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Failed),
                          std::memory_order_release);
    }
    return result;
}

PcmResult PcmStreamBroker::start(std::uint32_t handle) noexcept {
    Slot* slot = find_control(handle);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(PcmStreamState::Configured)) {
        return PcmResult::InvalidState;
    }
    slot->generation.fetch_add(1U, std::memory_order_acq_rel);
    slot->backend_stop_required = true;
    const PcmResult result = backend_.start(
        slot->backend_token.load(std::memory_order_relaxed));
    if (result == PcmResult::Ok) {
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Started),
                          std::memory_order_release);
        slot->transfer_gate.store(kTransferGateOpen, std::memory_order_release);
    } else {
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Failed),
                          std::memory_order_release);
    }
    return result;
}

PcmTransferResult PcmStreamBroker::write_transfer(
    std::uint32_t handle, const std::int16_t* interleaved,
    std::size_t frame_count) noexcept {
    if (handle == 0U) return transfer_error(PcmResult::NotOpen);
    if (interleaved == nullptr || frame_count == 0U) {
        return transfer_error(PcmResult::InvalidArgument);
    }
    if (frame_count > kMaxPcmWriteFrames) return transfer_error(PcmResult::BufferTooLarge);

    for (auto& slot : streams_) {
        const auto generation = slot.generation.load(std::memory_order_acquire);
        if (slot.handle.load(std::memory_order_acquire) != handle) continue;
        std::uint32_t gate = slot.transfer_gate.load(std::memory_order_acquire);
        for (;;) {
            if ((gate & kTransferGateOpen) == 0U) {
                return transfer_error(PcmResult::InvalidState);
            }
            if ((gate & kTransferCountMask) == kTransferCountMask) {
                return transfer_error(PcmResult::CapacityExceeded);
            }
            if (slot.transfer_gate.compare_exchange_weak(
                    gate, gate + 1U, std::memory_order_acq_rel,
                    std::memory_order_acquire)) break;
        }
        if (slot.generation.load(std::memory_order_acquire) != generation ||
            slot.handle.load(std::memory_order_acquire) != handle) {
            slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
            return transfer_error(PcmResult::NotOpen);
        }
        if (slot.state.load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(PcmStreamState::Started)) {
            slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
            return transfer_error(PcmResult::InvalidState);
        }
        if (slot.direction != PcmDirection::Playback) {
            slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
            return transfer_error(PcmResult::InvalidState);
        }
        const auto token = slot.backend_token.load(std::memory_order_relaxed);
        PcmTransferResult result = backend_.write_transfer(token, interleaved, frame_count);
        const bool invalid_result = result.frames_transferred > frame_count ||
            (result.result == PcmResult::Ok && result.frames_transferred == 0U);
        const bool terminal_error = result.result == PcmResult::Xrun ||
                                    result.result == PcmResult::DeviceLost;
        if (invalid_result || terminal_error) {
            slot.state.store(static_cast<std::uint32_t>(PcmStreamState::Failed),
                             std::memory_order_release);
            slot.transfer_gate.fetch_and(kTransferCountMask, std::memory_order_acq_rel);
        }
        slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
        return invalid_result ? transfer_error(PcmResult::BackendFailure) : result;
    }
    return transfer_error(PcmResult::NotOpen);
}

PcmTransferResult PcmStreamBroker::read_transfer(
    std::uint32_t handle, std::int16_t* interleaved,
    std::size_t frame_count) noexcept {
    if (handle == 0U) return transfer_error(PcmResult::NotOpen);
    if (interleaved == nullptr || frame_count == 0U) {
        return transfer_error(PcmResult::InvalidArgument);
    }
    if (frame_count > kMaxPcmWriteFrames) return transfer_error(PcmResult::BufferTooLarge);

    for (auto& slot : streams_) {
        const auto generation = slot.generation.load(std::memory_order_acquire);
        if (slot.handle.load(std::memory_order_acquire) != handle) continue;
        std::uint32_t gate = slot.transfer_gate.load(std::memory_order_acquire);
        for (;;) {
            if ((gate & kTransferGateOpen) == 0U) {
                return transfer_error(PcmResult::InvalidState);
            }
            if ((gate & kTransferCountMask) == kTransferCountMask) {
                return transfer_error(PcmResult::CapacityExceeded);
            }
            if (slot.transfer_gate.compare_exchange_weak(
                    gate, gate + 1U, std::memory_order_acq_rel,
                    std::memory_order_acquire)) break;
        }
        if (slot.generation.load(std::memory_order_acquire) != generation ||
            slot.handle.load(std::memory_order_acquire) != handle) {
            slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
            return transfer_error(PcmResult::NotOpen);
        }
        if (slot.state.load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(PcmStreamState::Started)) {
            slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
            return transfer_error(PcmResult::InvalidState);
        }
        if (slot.direction != PcmDirection::Capture) {
            slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
            return transfer_error(PcmResult::InvalidState);
        }
        const auto token = slot.backend_token.load(std::memory_order_relaxed);
        PcmTransferResult result = backend_.read_transfer(token, interleaved, frame_count);
        const bool invalid_result = result.frames_transferred > frame_count ||
            (result.result == PcmResult::Ok && result.frames_transferred == 0U);
        const bool terminal_error = result.result == PcmResult::Xrun ||
                                    result.result == PcmResult::DeviceLost;
        if (invalid_result || terminal_error) {
            slot.state.store(static_cast<std::uint32_t>(PcmStreamState::Failed),
                             std::memory_order_release);
            slot.transfer_gate.fetch_and(kTransferCountMask, std::memory_order_acq_rel);
        }
        slot.transfer_gate.fetch_sub(1U, std::memory_order_release);
        return invalid_result ? transfer_error(PcmResult::BackendFailure) : result;
    }
    return transfer_error(PcmResult::NotOpen);
}

PcmResult PcmStreamBroker::write(std::uint32_t handle,
                                 const std::int16_t* interleaved,
                                 std::size_t frame_count) noexcept {
    const PcmTransferResult result = write_transfer(handle, interleaved, frame_count);
    if (result.result != PcmResult::Ok) return result.result;
    return result.frames_transferred == frame_count ? PcmResult::Ok : PcmResult::BackendFailure;
}

PcmResult PcmStreamBroker::read(std::uint32_t handle,
                                std::int16_t* interleaved,
                                std::size_t frame_count) noexcept {
    const PcmTransferResult result = read_transfer(handle, interleaved, frame_count);
    if (result.result != PcmResult::Ok) return result.result;
    return result.frames_transferred == frame_count ? PcmResult::Ok : PcmResult::BackendFailure;
}

PcmResult PcmStreamBroker::stop(std::uint32_t handle) noexcept {
    Slot* slot = find_control(handle);
    if (slot == nullptr) return PcmResult::NotOpen;
    const PcmStreamState state = static_cast<PcmStreamState>(
        slot->state.load(std::memory_order_acquire));
    if (state == PcmStreamState::Stopped) return PcmResult::Ok;
    if ((state != PcmStreamState::Started && state != PcmStreamState::Failed) ||
        !slot->backend_stop_required) {
        return PcmResult::InvalidState;
    }

    const std::uint32_t previous = slot->transfer_gate.fetch_and(
        kTransferCountMask, std::memory_order_acq_rel);
    slot->generation.fetch_add(1U, std::memory_order_acq_rel);
    if ((previous & kTransferCountMask) != 0U) return PcmResult::WouldBlock;

    const PcmResult result = backend_.stop(
        slot->backend_token.load(std::memory_order_relaxed));
    if (result == PcmResult::Ok) slot->backend_stop_required = false;
    slot->state.store(static_cast<std::uint32_t>(result == PcmResult::Ok
                          ? PcmStreamState::Stopped : PcmStreamState::Failed),
                      std::memory_order_release);
    return result;
}

PcmResult PcmStreamBroker::close(std::uint32_t handle) noexcept {
    Slot* slot = find_control(handle);
    if (slot == nullptr) return PcmResult::NotOpen;
    if (slot->state.load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(PcmStreamState::Started) ||
        slot->backend_stop_required) return PcmResult::InvalidState;
    if ((slot->transfer_gate.load(std::memory_order_acquire) &
         kTransferCountMask) != 0U) return PcmResult::WouldBlock;

    const PcmResult result = backend_.close(
        slot->backend_token.load(std::memory_order_relaxed));
    if (result == PcmResult::Ok) {
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Closed),
                          std::memory_order_release);
        slot->handle.store(0U, std::memory_order_release);
        slot->backend_token.store(0U, std::memory_order_relaxed);
        slot->backend_stop_required = false;
        slot->transfer_gate.store(0U, std::memory_order_relaxed);
        slot->source = SourceId::None;
        slot->route = RouteType::None;
        slot->direction = PcmDirection::Playback;
        slot->config = PcmConfig{};
    } else {
        slot->state.store(static_cast<std::uint32_t>(PcmStreamState::Failed),
                          std::memory_order_release);
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
    out->direction = slot->direction;
    out->config = slot->config;
    out->state = static_cast<PcmStreamState>(slot->state.load(std::memory_order_acquire));
    out->backend_stop_required = slot->backend_stop_required;
    return true;
}

std::size_t PcmStreamBroker::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : streams_) {
        if (slot.state.load(std::memory_order_acquire) !=
            static_cast<std::uint32_t>(PcmStreamState::Closed)) ++count;
    }
    return count;
}

}  // namespace mt8163_audio_core
