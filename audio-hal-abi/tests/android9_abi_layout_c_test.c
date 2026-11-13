#include "mt8163_android9_abi_layout32.h"

int main(void) {
    return sizeof(mt8163_factory_audio_hw_device_layout32_t) ==
                   MT8163_FACTORY_AUDIO_HW_DEVICE_SIZE &&
               sizeof(mt8163_factory_stream_out_layout32_t) ==
                   MT8163_FACTORY_STREAM_OUT_SIZE &&
               sizeof(mt8163_factory_stream_in_layout32_t) ==
                   MT8163_FACTORY_STREAM_IN_SIZE
           ? 0
           : 1;
}
