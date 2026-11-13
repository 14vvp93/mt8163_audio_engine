#ifndef MT8163_ANDROID9_ABI_LAYOUT32_H
#define MT8163_ANDROID9_ABI_LAYOUT32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define MT8163_ABI_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define MT8163_ABI_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Layout-only mirror of the recovered Android 9 ARM32 factory ABI.
 * These 32-bit words model pointers/function pointers; do not cast them
 * to native pointers or use these structures as callable HAL objects.
 */
typedef uint32_t mt8163_abi_ptr32_t;

typedef struct mt8163_hw_module_layout32 {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    mt8163_abi_ptr32_t id;
    mt8163_abi_ptr32_t name;
    mt8163_abi_ptr32_t author;
    mt8163_abi_ptr32_t methods;
    mt8163_abi_ptr32_t dso;
    uint32_t reserved[25];
} mt8163_hw_module_layout32_t;

typedef struct mt8163_hw_module_methods_layout32 {
    mt8163_abi_ptr32_t open;
} mt8163_hw_module_methods_layout32_t;

typedef struct mt8163_hw_device_layout32 {
    uint32_t tag;
    uint32_t version;
    mt8163_abi_ptr32_t module;
    uint32_t reserved[12];
    mt8163_abi_ptr32_t close;
} mt8163_hw_device_layout32_t;

typedef struct mt8163_audio_stream_layout32 {
    mt8163_abi_ptr32_t callbacks[14];
} mt8163_audio_stream_layout32_t;

typedef struct mt8163_audio_stream_out_layout32 {
    mt8163_audio_stream_layout32_t common;
    mt8163_abi_ptr32_t callbacks[16];
} mt8163_audio_stream_out_layout32_t;

typedef struct mt8163_audio_stream_in_layout32 {
    mt8163_audio_stream_layout32_t common;
    mt8163_abi_ptr32_t callbacks[10];
} mt8163_audio_stream_in_layout32_t;

typedef struct mt8163_audio_hw_device_layout32 {
    mt8163_hw_device_layout32_t common;
    mt8163_abi_ptr32_t callbacks[23];
} mt8163_audio_hw_device_layout32_t;

typedef struct mt8163_factory_audio_hw_device_layout32 {
    mt8163_audio_hw_device_layout32_t public_device;
    mt8163_abi_ptr32_t vendor_callbacks[16];
    mt8163_abi_ptr32_t legacy_hardware;
} mt8163_factory_audio_hw_device_layout32_t;

typedef struct mt8163_factory_stream_out_layout32 {
    mt8163_audio_stream_out_layout32_t public_stream;
    mt8163_abi_ptr32_t legacy_out;
} mt8163_factory_stream_out_layout32_t;

typedef struct mt8163_factory_stream_in_layout32 {
    mt8163_audio_stream_in_layout32_t public_stream;
    mt8163_abi_ptr32_t legacy_in;
} mt8163_factory_stream_in_layout32_t;

enum {
    MT8163_FACTORY_MODULE_API_VERSION = 0x0001,
    MT8163_FACTORY_HAL_API_VERSION = 0x0100,
    MT8163_FACTORY_DEVICE_API_VERSION = 0x0300
};

enum mt8163_audio_stream_callback_index {
    MT8163_STREAM_GET_SAMPLE_RATE = 0,
    MT8163_STREAM_SET_SAMPLE_RATE,
    MT8163_STREAM_GET_BUFFER_SIZE,
    MT8163_STREAM_GET_CHANNELS,
    MT8163_STREAM_GET_FORMAT,
    MT8163_STREAM_SET_FORMAT,
    MT8163_STREAM_STANDBY,
    MT8163_STREAM_DUMP,
    MT8163_STREAM_GET_DEVICE,
    MT8163_STREAM_SET_DEVICE,
    MT8163_STREAM_SET_PARAMETERS,
    MT8163_STREAM_GET_PARAMETERS,
    MT8163_STREAM_ADD_AUDIO_EFFECT,
    MT8163_STREAM_REMOVE_AUDIO_EFFECT
};

enum mt8163_audio_stream_out_callback_index {
    MT8163_STREAM_OUT_GET_LATENCY = 0,
    MT8163_STREAM_OUT_SET_VOLUME,
    MT8163_STREAM_OUT_WRITE,
    MT8163_STREAM_OUT_GET_RENDER_POSITION,
    MT8163_STREAM_OUT_GET_NEXT_WRITE_TIMESTAMP,
    MT8163_STREAM_OUT_SET_CALLBACK,
    MT8163_STREAM_OUT_PAUSE,
    MT8163_STREAM_OUT_RESUME,
    MT8163_STREAM_OUT_DRAIN,
    MT8163_STREAM_OUT_FLUSH,
    MT8163_STREAM_OUT_GET_PRESENTATION_POSITION,
    MT8163_STREAM_OUT_START,
    MT8163_STREAM_OUT_STOP,
    MT8163_STREAM_OUT_CREATE_MMAP_BUFFER,
    MT8163_STREAM_OUT_GET_MMAP_POSITION,
    MT8163_STREAM_OUT_UPDATE_SOURCE_METADATA
};

enum mt8163_audio_stream_in_callback_index {
    MT8163_STREAM_IN_SET_GAIN = 0,
    MT8163_STREAM_IN_READ,
    MT8163_STREAM_IN_GET_INPUT_FRAMES_LOST,
    MT8163_STREAM_IN_GET_CAPTURE_POSITION,
    MT8163_STREAM_IN_START,
    MT8163_STREAM_IN_STOP,
    MT8163_STREAM_IN_CREATE_MMAP_BUFFER,
    MT8163_STREAM_IN_GET_MMAP_POSITION,
    MT8163_STREAM_IN_GET_ACTIVE_MICROPHONES,
    MT8163_STREAM_IN_UPDATE_SINK_METADATA
};

enum mt8163_audio_hw_device_callback_index {
    MT8163_DEVICE_GET_SUPPORTED_DEVICES = 0,
    MT8163_DEVICE_INIT_CHECK,
    MT8163_DEVICE_SET_VOICE_VOLUME,
    MT8163_DEVICE_SET_MASTER_VOLUME,
    MT8163_DEVICE_GET_MASTER_VOLUME,
    MT8163_DEVICE_SET_MODE,
    MT8163_DEVICE_SET_MIC_MUTE,
    MT8163_DEVICE_GET_MIC_MUTE,
    MT8163_DEVICE_SET_PARAMETERS,
    MT8163_DEVICE_GET_PARAMETERS,
    MT8163_DEVICE_GET_INPUT_BUFFER_SIZE,
    MT8163_DEVICE_OPEN_OUTPUT_STREAM,
    MT8163_DEVICE_CLOSE_OUTPUT_STREAM,
    MT8163_DEVICE_OPEN_INPUT_STREAM,
    MT8163_DEVICE_CLOSE_INPUT_STREAM,
    MT8163_DEVICE_GET_MICROPHONES,
    MT8163_DEVICE_DUMP,
    MT8163_DEVICE_SET_MASTER_MUTE,
    MT8163_DEVICE_GET_MASTER_MUTE,
    MT8163_DEVICE_CREATE_AUDIO_PATCH,
    MT8163_DEVICE_RELEASE_AUDIO_PATCH,
    MT8163_DEVICE_GET_AUDIO_PORT,
    MT8163_DEVICE_SET_AUDIO_PORT_CONFIG
};

enum mt8163_factory_vendor_callback_index {
    MT8163_VENDOR_XWAY_PLAY_START = 0,
    MT8163_VENDOR_XWAY_PLAY_STOP,
    MT8163_VENDOR_XWAY_PLAY_WRITE,
    MT8163_VENDOR_XWAY_PLAY_GET_FREE_BUFFER_COUNT,
    MT8163_VENDOR_XWAY_REC_START,
    MT8163_VENDOR_XWAY_REC_STOP,
    MT8163_VENDOR_XWAY_REC_READ,
    MT8163_VENDOR_READ_REF_FROM_RING,
    MT8163_VENDOR_GET_VOICE_UNLOCK_UL_TIME,
    MT8163_VENDOR_SET_VOICE_UNLOCK_SRC,
    MT8163_VENDOR_START_VOICE_UNLOCK_DL,
    MT8163_VENDOR_STOP_VOICE_UNLOCK_DL,
    MT8163_VENDOR_GET_VOICE_UNLOCK_DL_INSTANCE,
    MT8163_VENDOR_SETUP_PARAMETERS_CALLBACK,
    MT8163_VENDOR_SET_AUDIO_PARAMETER_CHANGED_CALLBACK,
    MT8163_VENDOR_CLEAR_AUDIO_PARAMETER_CHANGED_CALLBACK
};

enum {
    MT8163_HW_MODULE_SIZE = 128,
    MT8163_HW_DEVICE_SIZE = 64,
    MT8163_AUDIO_STREAM_SIZE = 56,
    MT8163_AUDIO_STREAM_OUT_SIZE = 120,
    MT8163_AUDIO_STREAM_IN_SIZE = 96,
    MT8163_AUDIO_HW_DEVICE_SIZE = 156,
    MT8163_FACTORY_AUDIO_HW_DEVICE_SIZE = 224,
    MT8163_FACTORY_STREAM_OUT_SIZE = 124,
    MT8163_FACTORY_STREAM_IN_SIZE = 100,

    MT8163_AUDIO_HW_DEVICE_CALLBACKS_OFFSET = 64,
    MT8163_FACTORY_DEVICE_VENDOR_CALLBACKS_OFFSET = 156,
    MT8163_FACTORY_DEVICE_LEGACY_HARDWARE_OFFSET = 220,
    MT8163_FACTORY_STREAM_OUT_LEGACY_OFFSET = 120,
    MT8163_FACTORY_STREAM_IN_LEGACY_OFFSET = 96
};

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_hw_module_methods_layout32_t) == 4,
                         "hw_module_methods_t ARM32 size mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_STREAM_REMOVE_AUDIO_EFFECT == 13,
                         "audio_stream callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_STREAM_OUT_UPDATE_SOURCE_METADATA == 15,
                         "audio_stream_out callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_STREAM_IN_UPDATE_SINK_METADATA == 9,
                         "audio_stream_in callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_DEVICE_SET_AUDIO_PORT_CONFIG == 22,
                         "audio_hw_device callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_XWAY_PLAY_START == 0,
                         "xWay playback start callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_XWAY_PLAY_STOP == 1,
                         "xWay playback stop callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_XWAY_PLAY_WRITE == 2,
                         "xWay playback write callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_XWAY_PLAY_GET_FREE_BUFFER_COUNT == 3,
                         "xWay playback free-buffer callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_XWAY_REC_START == 4,
                         "xWay recording start callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_XWAY_REC_STOP == 5,
                         "xWay recording stop callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_XWAY_REC_READ == 6,
                         "xWay recording read callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_READ_REF_FROM_RING == 7,
                         "reference-ring callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_GET_VOICE_UNLOCK_UL_TIME == 8,
                         "voice-unlock time callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_SET_VOICE_UNLOCK_SRC == 9,
                         "voice-unlock SRC callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_START_VOICE_UNLOCK_DL == 10,
                         "voice-unlock start callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_STOP_VOICE_UNLOCK_DL == 11,
                         "voice-unlock stop callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_GET_VOICE_UNLOCK_DL_INSTANCE == 12,
                         "voice-unlock instance callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_SETUP_PARAMETERS_CALLBACK == 13,
                         "parameter setup callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_SET_AUDIO_PARAMETER_CHANGED_CALLBACK == 14,
                         "parameter-change registration callback index mismatch");
MT8163_ABI_STATIC_ASSERT(MT8163_VENDOR_CLEAR_AUDIO_PARAMETER_CHANGED_CALLBACK == 15,
                         "parameter-change clear callback index mismatch");

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_hw_module_layout32_t) == MT8163_HW_MODULE_SIZE,
                         "hw_module_t ARM32 size mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_hw_module_layout32_t, module_api_version) == 4,
                         "hw_module_t module_api_version offset mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_hw_module_layout32_t, id) == 8,
                         "hw_module_t id offset mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_hw_module_layout32_t, methods) == 20,
                         "hw_module_t methods offset mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_hw_module_layout32_t, dso) == 24,
                         "hw_module_t dso offset mismatch");

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_hw_device_layout32_t) == MT8163_HW_DEVICE_SIZE,
                         "hw_device_t ARM32 size mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_hw_device_layout32_t, close) == 60,
                         "hw_device_t close offset mismatch");

MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_hw_device_layout32_t, module) == 8,
                         "hw_device_t module offset mismatch");

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_audio_stream_layout32_t) == MT8163_AUDIO_STREAM_SIZE,
                         "audio_stream ARM32 size mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_audio_stream_out_layout32_t, callbacks) == 56,
                         "audio_stream_out callback table offset mismatch");
MT8163_ABI_STATIC_ASSERT(
    offsetof(mt8163_audio_stream_out_layout32_t, callbacks) +
        MT8163_STREAM_OUT_UPDATE_SOURCE_METADATA * sizeof(mt8163_abi_ptr32_t) == 116,
    "audio_stream_out metadata callback offset mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_audio_stream_in_layout32_t, callbacks) == 56,
                         "audio_stream_in callback table offset mismatch");
MT8163_ABI_STATIC_ASSERT(
    offsetof(mt8163_audio_stream_in_layout32_t, callbacks) +
        MT8163_STREAM_IN_UPDATE_SINK_METADATA * sizeof(mt8163_abi_ptr32_t) == 92,
    "audio_stream_in metadata callback offset mismatch");
MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_audio_stream_out_layout32_t) == MT8163_AUDIO_STREAM_OUT_SIZE,
                         "audio_stream_out ARM32 size mismatch");
MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_audio_stream_in_layout32_t) == MT8163_AUDIO_STREAM_IN_SIZE,
                         "audio_stream_in ARM32 size mismatch");

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_audio_hw_device_layout32_t) ==
                             MT8163_AUDIO_HW_DEVICE_SIZE,
                         "audio_hw_device ARM32 size mismatch");
MT8163_ABI_STATIC_ASSERT(offsetof(mt8163_audio_hw_device_layout32_t, callbacks) ==
                             MT8163_AUDIO_HW_DEVICE_CALLBACKS_OFFSET,
                         "audio_hw_device callback table offset mismatch");

MT8163_ABI_STATIC_ASSERT(
    sizeof(mt8163_factory_audio_hw_device_layout32_t) ==
        MT8163_FACTORY_AUDIO_HW_DEVICE_SIZE,
    "factory audio_hw_device wrapper size mismatch");
MT8163_ABI_STATIC_ASSERT(
    offsetof(mt8163_factory_audio_hw_device_layout32_t, vendor_callbacks) ==
        MT8163_FACTORY_DEVICE_VENDOR_CALLBACKS_OFFSET,
    "factory vendor callback tail offset mismatch");
MT8163_ABI_STATIC_ASSERT(
    offsetof(mt8163_factory_audio_hw_device_layout32_t, legacy_hardware) ==
        MT8163_FACTORY_DEVICE_LEGACY_HARDWARE_OFFSET,
    "factory legacy hardware pointer offset mismatch");

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_factory_stream_out_layout32_t) ==
                             MT8163_FACTORY_STREAM_OUT_SIZE,
                         "factory output stream wrapper size mismatch");
MT8163_ABI_STATIC_ASSERT(
    offsetof(mt8163_factory_stream_out_layout32_t, legacy_out) ==
        MT8163_FACTORY_STREAM_OUT_LEGACY_OFFSET,
    "factory output legacy pointer offset mismatch");

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_factory_stream_in_layout32_t) ==
                             MT8163_FACTORY_STREAM_IN_SIZE,
                         "factory input stream wrapper size mismatch");
MT8163_ABI_STATIC_ASSERT(
    offsetof(mt8163_factory_stream_in_layout32_t, legacy_in) ==
        MT8163_FACTORY_STREAM_IN_LEGACY_OFFSET,
    "factory input legacy pointer offset mismatch");

#ifdef __cplusplus
}
#endif

#undef MT8163_ABI_STATIC_ASSERT

#endif
