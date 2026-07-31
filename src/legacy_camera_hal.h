#ifndef LINEAGEOS_CAMERA_LEGACY_CAMERA_HAL_H
#define LINEAGEOS_CAMERA_LEGACY_CAMERA_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct native_handle native_handle_t;
typedef const native_handle_t *buffer_handle_t;
typedef struct camera_metadata camera_metadata_t;
typedef struct camera_frame_metadata camera_frame_metadata_t;
typedef struct vendor_tag_ops vendor_tag_ops_t;

typedef struct hw_module_t hw_module_t;
typedef struct hw_device_t hw_device_t;

typedef struct hw_module_methods_t {
    int (*open)(const hw_module_t *module, const char *id, hw_device_t **device);
} hw_module_methods_t;

struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    hw_module_methods_t *methods;
    void *dso;
    uint32_t reserved[25];
};

struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    hw_module_t *module;
    uint32_t reserved[12];
    int (*close)(hw_device_t *device);
};

typedef struct camera_info {
    int facing;
    int orientation;
    uint32_t device_version;
    const camera_metadata_t *static_camera_characteristics;
    int resource_cost;
    char **conflicting_devices;
    size_t conflicting_devices_length;
} camera_info_t;

typedef struct camera_module_callbacks camera_module_callbacks_t;
struct camera_module_callbacks {
    void (*camera_device_status_change)(const camera_module_callbacks_t *callbacks,
                                        int camera_id, int new_status);
    void (*torch_mode_status_change)(const camera_module_callbacks_t *callbacks,
                                     const char *camera_id, int new_status);
};

typedef struct camera_module {
    hw_module_t common;
    int (*get_number_of_cameras)(void);
    int (*get_camera_info)(int camera_id, camera_info_t *info);
    int (*set_callbacks)(const camera_module_callbacks_t *callbacks);
    void (*get_vendor_tag_ops)(vendor_tag_ops_t *ops);
    int (*open_legacy)(const hw_module_t *module, const char *id,
                       uint32_t hal_version, hw_device_t **device);
    int (*set_torch_mode)(const char *camera_id, bool enabled);
    int (*init)(void);
    void *reserved[5];
} camera_module_t;

typedef struct camera_memory camera_memory_t;
typedef void (*camera_release_memory)(camera_memory_t *memory);
struct camera_memory {
    void *data;
    size_t size;
    void *handle;
    camera_release_memory release;
};

typedef camera_memory_t *(*camera_request_memory)(int fd, size_t buffer_size,
                                                   unsigned int buffer_count,
                                                   void *user);
typedef void (*camera_notify_callback)(int32_t message_type, int32_t ext1,
                                       int32_t ext2, void *user);
typedef void (*camera_data_callback)(int32_t message_type,
                                     const camera_memory_t *data,
                                     unsigned int index,
                                     camera_frame_metadata_t *metadata,
                                     void *user);
typedef void (*camera_data_timestamp_callback)(int64_t timestamp,
                                               int32_t message_type,
                                               const camera_memory_t *data,
                                               unsigned int index,
                                               void *user);

typedef struct preview_stream_ops preview_stream_ops_t;
struct preview_stream_ops {
    int (*dequeue_buffer)(preview_stream_ops_t *window,
                          buffer_handle_t **buffer, int *stride);
    int (*enqueue_buffer)(preview_stream_ops_t *window, buffer_handle_t *buffer);
    int (*cancel_buffer)(preview_stream_ops_t *window, buffer_handle_t *buffer);
    int (*set_buffer_count)(preview_stream_ops_t *window, int count);
    int (*set_buffers_geometry)(preview_stream_ops_t *window, int width,
                                int height, int format);
    int (*set_crop)(preview_stream_ops_t *window, int left, int top,
                    int right, int bottom);
    int (*set_usage)(preview_stream_ops_t *window, int usage);
    int (*set_swap_interval)(preview_stream_ops_t *window, int interval);
    int (*get_min_undequeued_buffer_count)(const preview_stream_ops_t *window,
                                           int *count);
    int (*lock_buffer)(preview_stream_ops_t *window, buffer_handle_t *buffer);
    int (*set_timestamp)(preview_stream_ops_t *window, int64_t timestamp);
};

typedef struct camera_device camera_device_t;
typedef struct camera_device_ops {
    int (*set_preview_window)(camera_device_t *device,
                              preview_stream_ops_t *window);
    void (*set_callbacks)(camera_device_t *device,
                          camera_notify_callback notify_callback,
                          camera_data_callback data_callback,
                          camera_data_timestamp_callback timestamp_callback,
                          camera_request_memory request_memory,
                          void *user);
    void (*enable_msg_type)(camera_device_t *device, int32_t message_type);
    void (*disable_msg_type)(camera_device_t *device, int32_t message_type);
    int (*msg_type_enabled)(camera_device_t *device, int32_t message_type);
    int (*start_preview)(camera_device_t *device);
    void (*stop_preview)(camera_device_t *device);
    int (*preview_enabled)(camera_device_t *device);
    int (*store_meta_data_in_buffers)(camera_device_t *device, int enable);
    int (*start_recording)(camera_device_t *device);
    void (*stop_recording)(camera_device_t *device);
    int (*recording_enabled)(camera_device_t *device);
    void (*release_recording_frame)(camera_device_t *device,
                                    const void *opaque);
    int (*auto_focus)(camera_device_t *device);
    int (*cancel_auto_focus)(camera_device_t *device);
    int (*take_picture)(camera_device_t *device);
    int (*cancel_picture)(camera_device_t *device);
    int (*set_parameters)(camera_device_t *device, const char *parameters);
    char *(*get_parameters)(camera_device_t *device);
    void (*put_parameters)(camera_device_t *device, char *parameters);
    int (*send_command)(camera_device_t *device, int32_t command,
                        int32_t argument1, int32_t argument2);
    void (*release)(camera_device_t *device);
    int (*dump)(camera_device_t *device, int fd);
} camera_device_ops_t;

struct camera_device {
    hw_device_t common;
    camera_device_ops_t *ops;
    void *priv;
};

#define CAMERA_DEVICE_API_VERSION_1_0 0x0100u

#ifdef __cplusplus
}
#endif

#endif
