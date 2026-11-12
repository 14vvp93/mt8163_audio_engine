#include "mt8163_audio_core/route_policy.hpp"

#include <cstdlib>
#include <iostream>

using namespace mt8163_audio_core;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_proven_pcm_routes() {
    require(validate_source_route(SourceId::LocalMedia, RouteType::PrimaryPcm) ==
                RouteValidation::Supported,
            "local media uses primary PCM");
    require(validate_source_route(SourceId::Navigation, RouteType::PrimaryPcm) ==
                RouteValidation::Supported,
            "navigation uses primary PCM");
    require(validate_source_route(SourceId::Notification, RouteType::PrimaryPcm) ==
                RouteValidation::Supported,
            "notification uses primary PCM");
    require(validate_source_route(SourceId::BluetoothA2dp, RouteType::A2dpPcm) ==
                RouteValidation::Supported,
            "A2DP is separate from primary PCM");
    require(validate_source_route(SourceId::BluetoothHfp, RouteType::HfpSco) ==
                RouteValidation::Supported,
            "HFP is separate from A2DP");
    require(validate_source_route(SourceId::Microphone, RouteType::PrimaryCapturePcm) ==
                RouteValidation::Supported,
            "microphone input uses primary capture PCM");
}

void test_fm_planes_remain_distinct() {
    require(validate_source_route(SourceId::FmRadio, RouteType::FmDirectI2s) ==
                RouteValidation::Supported,
            "FM direct I2S is recognized");
    require(validate_source_route(SourceId::FmRadio, RouteType::FmCapturePcm) ==
                RouteValidation::Supported,
            "FM capture PCM is recognized");
    require(validate_source_route(SourceId::FmRadio, RouteType::Cd3313AnalogInput) ==
                RouteValidation::Supported,
            "FM CD3313 analog control/input plane is recognized");
    require(validate_source_route(SourceId::FmRadio, RouteType::PrimaryPcm) ==
                RouteValidation::Unverified,
            "FM is not silently collapsed into primary PCM");
}

void test_cd3313_analog_inputs() {
    require(validate_source_route(SourceId::Aux, RouteType::Cd3313AnalogInput) ==
                RouteValidation::Supported,
            "AUX maps to CD3313 analog input");
    require(validate_source_route(SourceId::Aux2, RouteType::Cd3313AnalogInput) ==
                RouteValidation::Supported,
            "AUX2 maps to CD3313 analog input");
    require(validate_source_route(SourceId::Dab, RouteType::Cd3313AnalogInput) ==
                RouteValidation::Supported,
            "DAB maps to CD3313 analog input");
    require(validate_source_route(SourceId::Aux, RouteType::PrimaryPcm) ==
                RouteValidation::Unverified,
            "analog AUX is not assumed to be a PCM stream");
}

void test_unproven_and_invalid_pairs() {
    require(validate_source_route(SourceId::Reverse, RouteType::PrimaryPcm) ==
                RouteValidation::Unverified,
            "reverse route remains unverified");
    require(validate_source_route(SourceId::OnStar, RouteType::HfpSco) ==
                RouteValidation::Unverified,
            "OnStar route remains unverified");
    require(validate_source_route(SourceId::LocalMedia, RouteType::None) ==
                RouteValidation::Invalid,
            "NONE route is invalid");
    require(validate_source_route(SourceId::Count, RouteType::PrimaryPcm) ==
                RouteValidation::Invalid,
            "out-of-range source is invalid");
}
}  // namespace

int main() {
    test_proven_pcm_routes();
    test_fm_planes_remain_distinct();
    test_cd3313_analog_inputs();
    test_unproven_and_invalid_pairs();
    std::cout << "AudioCore route policy tests passed\n";
    return 0;
}
