#pragma once

#include "mt8163_audio_core/types.hpp"

#include <cstdint>

namespace mt8163_audio_core {

// Describes whether a source/route pair exposes samples to the unified DSP.
// This is independent from route validity: a physically valid analog route
// may still have no software-visible PCM boundary.
enum class DspRouteClass : std::uint8_t {
    PcmPlayback = 0,
    PcmPlaybackRequiresOffloadControl,
    PcmCapture,
    CaptureRequiresEchoReference,
    TelephonyRequiresPcmBridge,
    RequiresDigitalTap,
    AnalogOnlyNoPcmEvidence,
    Unverified,
    Invalid
};

DspRouteClass classify_dsp_route(SourceId source,
                                 RouteType route) noexcept;

}  // namespace mt8163_audio_core
