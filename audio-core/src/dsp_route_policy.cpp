#include "mt8163_audio_core/dsp_route_policy.hpp"

namespace mt8163_audio_core {
namespace {

bool valid_source(SourceId source) noexcept {
    return source > SourceId::None && source < SourceId::Count;
}

bool valid_route(RouteType route) noexcept {
    switch (route) {
        case RouteType::PrimaryPcm:
        case RouteType::PrimaryCapturePcm:
        case RouteType::A2dpPcm:
        case RouteType::HfpSco:
        case RouteType::FmDirectI2s:
        case RouteType::FmCapturePcm:
        case RouteType::Cd3313AnalogInput:
        case RouteType::HdmiPcm:
            return true;
        case RouteType::None:
        default:
            return false;
    }
}

}  // namespace

DspRouteClass classify_dsp_route(SourceId source,
                                 RouteType route) noexcept {
    if (!valid_source(source) || !valid_route(route)) {
        return DspRouteClass::Invalid;
    }

    switch (source) {
        case SourceId::LocalMedia:
        case SourceId::Navigation:
        case SourceId::Notification:
        case SourceId::SystemSound:
            return route == RouteType::PrimaryPcm
                ? DspRouteClass::PcmPlayback
                : DspRouteClass::Unverified;

        case SourceId::BluetoothA2dp:
            return route == RouteType::A2dpPcm
                ? DspRouteClass::PcmPlaybackRequiresOffloadControl
                : DspRouteClass::Unverified;

        case SourceId::BluetoothHfp:
            return route == RouteType::HfpSco
                ? DspRouteClass::TelephonyRequiresPcmBridge
                : DspRouteClass::Unverified;

        case SourceId::Microphone:
            return route == RouteType::PrimaryCapturePcm
                ? DspRouteClass::CaptureRequiresEchoReference
                : DspRouteClass::Unverified;

        case SourceId::FmRadio:
            if (route == RouteType::FmCapturePcm) {
                return DspRouteClass::PcmCapture;
            }
            if (route == RouteType::FmDirectI2s) {
                return DspRouteClass::RequiresDigitalTap;
            }
            if (route == RouteType::Cd3313AnalogInput) {
                return DspRouteClass::AnalogOnlyNoPcmEvidence;
            }
            return DspRouteClass::Unverified;

        case SourceId::Aux:
        case SourceId::Aux2:
        case SourceId::Dab:
            return route == RouteType::Cd3313AnalogInput
                ? DspRouteClass::AnalogOnlyNoPcmEvidence
                : DspRouteClass::Unverified;

        case SourceId::Reverse:
        case SourceId::OnStar:
        case SourceId::None:
        case SourceId::SystemSound:
        case SourceId::Count:
        default:
            return DspRouteClass::Unverified;
    }
}

}  // namespace mt8163_audio_core
