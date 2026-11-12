#include "mt8163_audio_core/pcm_backend.hpp"

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

void test_route_boundary() {
    require(is_pcm_transport_route(RouteType::PrimaryPcm), "primary PCM is a PCM route");
    require(is_pcm_transport_route(RouteType::A2dpPcm), "A2DP is a PCM route");
    require(is_pcm_transport_route(RouteType::HfpSco), "HFP is a PCM route");
    require(is_pcm_transport_route(RouteType::FmCapturePcm), "FM capture is a PCM route");
    require(!is_pcm_transport_route(RouteType::FmDirectI2s), "direct I2S is not a PCM route");
    require(!is_pcm_transport_route(RouteType::Cd3313AnalogInput),
            "CD3313 analog input is not a PCM route");
    require(!is_pcm_transport_route(RouteType::None), "NONE is not a PCM route");
}

void test_direction_boundary() {
    require(is_valid_pcm_route_direction(RouteType::PrimaryPcm, PcmDirection::Playback),
            "primary route is playback");
    require(!is_valid_pcm_route_direction(RouteType::PrimaryPcm, PcmDirection::Capture),
            "primary output route is not capture");
    require(is_valid_pcm_route_direction(RouteType::HfpSco, PcmDirection::Playback) &&
            is_valid_pcm_route_direction(RouteType::HfpSco, PcmDirection::Capture),
            "HFP has independent downlink and uplink directions");
    require(is_valid_pcm_route_direction(RouteType::FmCapturePcm, PcmDirection::Capture),
            "FM capture route is capture");
    require(!is_valid_pcm_route_direction(RouteType::FmCapturePcm, PcmDirection::Playback),
            "FM capture cannot be opened as playback");
    require(!is_valid_pcm_route_direction(RouteType::FmDirectI2s, PcmDirection::Playback),
            "direct I2S remains outside PCM transport");
}

void test_pcm_config_validation() {
    PcmConfig config{44100U, 2U, 256U};
    require(is_valid_pcm_config(config), "valid PCM16 stereo configuration accepted");
    config.sample_rate_hz = 0U;
    require(!is_valid_pcm_config(config), "zero sample rate rejected");
    config = PcmConfig{48000U, 4U, 256U};
    require(!is_valid_pcm_config(config), "unproven four-channel PCM rejected");
    config = PcmConfig{48000U, 2U, 0U};
    require(!is_valid_pcm_config(config), "zero period rejected");
}
}  // namespace

int main() {
    test_route_boundary();
    test_direction_boundary();
    test_pcm_config_validation();
    std::cout << "PCM backend contract tests passed\n";
    return 0;
}
