#pragma once

#include <cstdint>
#include <type_traits>

namespace mt8163_audio_core {

struct SessionToken {
    std::uint32_t handle{0U};
    std::uint64_t generation{0U};

    constexpr bool valid() const noexcept {
        return handle != 0U && generation != 0U;
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(SessionToken lhs,
                                     SessionToken rhs) noexcept {
        return lhs.handle == rhs.handle &&
               lhs.generation == rhs.generation;
    }

    friend constexpr bool operator!=(SessionToken lhs,
                                     SessionToken rhs) noexcept {
        return !(lhs == rhs);
    }
};

static_assert(std::is_trivially_copyable<SessionToken>::value,
              "SessionToken must remain a trivially copyable value type");

}  // namespace mt8163_audio_core
