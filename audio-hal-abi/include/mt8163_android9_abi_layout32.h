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

MT8163_ABI_STATIC_ASSERT(sizeof(mt8163_audio_stream_layout32_t) == MT8163_AUDIO_STREAM_SIZE,
                         "audio_stream ARM32 size mismatch");
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
