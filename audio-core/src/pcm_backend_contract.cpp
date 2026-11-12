#include "mt8163_audio_core/pcm_backend.hpp"

#include <type_traits>

static_assert(std::is_nothrow_destructible<mt8163_audio_core::PcmBackend>::value,
              "PCM backend lifetime must be noexcept");
static_assert(mt8163_audio_core::is_pcm_transport_route(
                  mt8163_audio_core::RouteType::PrimaryPcm),
              "primary route must remain a PCM transport");
