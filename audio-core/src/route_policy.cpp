#include "mt8163_audio_core/route_policy.hpp"

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

RouteValidation validate_source_route(SourceId source,
                                      RouteType route) noexcept {
    if (!valid_source(source) || !valid_route(route)) {
        return RouteValidation::Invalid;
    }

    switch (source) {
        case SourceId::LocalMedia:
        case SourceId::Navigation:
        case SourceId::Notification:
            return route == RouteType::PrimaryPcm
                ? RouteValidation::Supported
                : RouteValidation::Unverified;

        case SourceId::BluetoothA2dp:
            return route == RouteType::A2dpPcm
                ? RouteValidation::Supported
                : RouteValidation::Unverified;

        case SourceId::BluetoothHfp:
            return route == RouteType::HfpSco
                ? RouteValidation::Supported
                : RouteValidation::Unverified;

        case SourceId::FmRadio:
            return (route == RouteType::FmDirectI2s ||
                    route == RouteType::FmCapturePcm ||
                    route == RouteType::Cd3313AnalogInput)
                ? RouteValidation::Supported
                : RouteValidation::Unverified;

        case SourceId::Aux:
        case SourceId::Aux2:
        case SourceId::Dab:
            return route == RouteType::Cd3313AnalogInput
                ? RouteValidation::Supported
                : RouteValidation::Unverified;

        case SourceId::Microphone:
            return route == RouteType::PrimaryCapturePcm
                ? RouteValidation::Supported
                : RouteValidation::Unverified;

        case SourceId::Reverse:
        case SourceId::OnStar:
        case SourceId::None:
        case SourceId::Count:
        default:
            return RouteValidation::Unverified;
    }
}

}  // namespace mt8163_audio_core
