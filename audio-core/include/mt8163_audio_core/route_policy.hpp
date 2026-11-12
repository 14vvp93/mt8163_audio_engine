#pragma once

#include "mt8163_audio_core/types.hpp"

#include <cstdint>

namespace mt8163_audio_core {

// Unverified means the enum values are valid, but the supplied firmware
// evidence does not yet prove this source/transport pairing. It is not a
// runtime permission to open an ALSA endpoint.
enum class RouteValidation : std::uint8_t {
    Supported = 0,
    Unverified,
    Invalid
};

RouteValidation validate_source_route(SourceId source,
                                      RouteType route) noexcept;

}  // namespace mt8163_audio_core
