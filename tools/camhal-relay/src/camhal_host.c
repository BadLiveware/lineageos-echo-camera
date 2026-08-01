#include "legacy_camera_hal.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_HAL_PATH "/data/local/tmp/camprobe/vendor/lib/hw/camera.mt8163.so"
#define DEFAULT_RELAY_SOCKET "/data/local/tmp/camprobe/camrelay.sock"
#define MAX_PARAMETER_BYTES (1024u * 1024u)
#define PREVIEW_WIDTH 640u
#define PREVIEW_HEIGHT 480u
#define PREVIEW_BYTES (PREVIEW_WIDTH * PREVIEW_HEIGHT * 3u / 2u)
#define RELAY_MAGIC 0x594c5243u
#define RELAY_PIXEL_FORMAT_NV21 0x3132564eu

_Static_assert(sizeof(hw_module_t) == 128, "unexpected 32-bit hw_module_t layout");
_Static_assert(sizeof(hw_device_t) == 64, "unexpected 32-bit hw_device_t layout");
_Static_assert(offsetof(camera_module_t, get_number_of_cameras) == 128,
               "unexpected camera_module_t layout");
_Static_assert(offsetof(camera_device_t, ops) == 64,
               "unexpected camera_device_t layout");

typedef struct host_state {
    void *dso;
    camera_module_t *module;
    camera_device_t *device;
} host_state_t;

typedef struct host_memory {
    camera_memory_t camera_memory;
    void *mapping;
    size_t mapping_size;
    size_t slot_size;
    unsigned int slot_count;
    int owned_fd;
} host_memory_t;

typedef struct __attribute__((packed)) relay_frame_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint64_t sequence;
    uint64_t timestamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t payload_bytes;
    uint32_t dropped_frames;
} relay_frame_header_t;

_Static_assert(sizeof(relay_frame_header_t) == 44,
               "unexpected relay frame header layout");

typedef struct relay_state {
    int listen_fd;
    int client_fd;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    unsigned char *packet;
    size_t packet_capacity;
    size_t packet_size;
    size_t packet_offset;
    uint64_t sequence;
    unsigned int frame_limit;
    pthread_mutex_t lock;
    atomic_uint frames_sent;
    atomic_uint frames_dropped;
} relay_state_t;

typedef struct capture_state {
    const char *output_path;
    relay_state_t *relay;
    size_t expected_frame_size;
    unsigned int minimum_callback;
    atomic_uint callbacks_received;
    atomic_int frame_status;
} capture_state_t;

static void device_status_changed(const camera_module_callbacks_t *callbacks,
                                  int camera_id, int new_status) {
    (void)callbacks;
    fprintf(stderr, "[camhal] module callback: camera %d status=%d\n",
            camera_id, new_status);
}

static void torch_status_changed(const camera_module_callbacks_t *callbacks,
                                 const char *camera_id, int new_status) {
    (void)callbacks;
    fprintf(stderr, "[camhal] module callback: torch %s status=%d\n",
            camera_id ? camera_id : "<null>", new_status);
}

static const camera_module_callbacks_t kModuleCallbacks = {
    .camera_device_status_change = device_status_changed,
    .torch_mode_status_change = torch_status_changed,
};

static void release_camera_memory(camera_memory_t *memory) {
    if (!memory || !memory->handle) {
        return;
    }

    host_memory_t *host_memory = (host_memory_t *)memory->handle;
    fprintf(stderr,
            "[camhal] releasing callback memory mapping=%p size=%zu slots=%u\n",
            host_memory->mapping, host_memory->mapping_size,
            host_memory->slot_count);
    if (host_memory->mapping && host_memory->mapping != MAP_FAILED) {
        munmap(host_memory->mapping, host_memory->mapping_size);
    }
    if (host_memory->owned_fd >= 0) {
        close(host_memory->owned_fd);
    }
    host_memory->camera_memory.handle = NULL;
    free(host_memory);
}

static camera_memory_t *request_camera_memory(int fd, size_t buffer_size,
                                               unsigned int buffer_count,
                                               void *user) {
    (void)user;
    if (buffer_size == 0 || buffer_count == 0 ||
        buffer_size > SIZE_MAX / buffer_count) {
        fprintf(stderr,
                "[camhal] rejecting callback memory request fd=%d size=%zu count=%u\n",
                fd, buffer_size, buffer_count);
        return NULL;
    }

    size_t total_size = buffer_size * buffer_count;
    if (total_size > 128u * 1024u * 1024u) {
        fprintf(stderr,
                "[camhal] rejecting oversized callback memory request: %zu bytes\n",
                total_size);
        return NULL;
    }

    host_memory_t *host_memory = calloc(1, sizeof(*host_memory));
    if (!host_memory) {
        return NULL;
    }
    host_memory->owned_fd = -1;

    int map_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    int map_fd = -1;
    if (fd >= 0) {
        host_memory->owned_fd = dup(fd);
        if (host_memory->owned_fd < 0) {
            fprintf(stderr, "[camhal] dup callback-memory fd failed: %s\n",
                    strerror(errno));
            free(host_memory);
            return NULL;
        }
        map_flags = MAP_SHARED;
        map_fd = host_memory->owned_fd;
    }

    void *mapping = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                         map_flags, map_fd, 0);
    if (mapping == MAP_FAILED) {
        fprintf(stderr,
                "[camhal] mmap callback memory failed fd=%d size=%zu: %s\n",
                fd, total_size, strerror(errno));
        if (host_memory->owned_fd >= 0) {
            close(host_memory->owned_fd);
        }
        free(host_memory);
        return NULL;
    }

    host_memory->mapping = mapping;
    host_memory->mapping_size = total_size;
    host_memory->slot_size = buffer_size;
    host_memory->slot_count = buffer_count;
    host_memory->camera_memory.data = mapping;
    host_memory->camera_memory.size = total_size;
    host_memory->camera_memory.handle = host_memory;
    host_memory->camera_memory.release = release_camera_memory;

    fprintf(stderr,
            "[camhal] callback memory fd=%d slot_size=%zu count=%u total=%zu mapping=%p\n",
            fd, buffer_size, buffer_count, total_size, mapping);
    return &host_memory->camera_memory;
}

static void camera_notify(int32_t message_type, int32_t ext1, int32_t ext2,
                          void *user) {
    (void)user;
    fprintf(stderr, "[camhal] notify type=0x%x ext1=%d ext2=%d\n",
            message_type, ext1, ext2);
}

static int write_all(int fd, const void *data, size_t size) {
    const unsigned char *cursor = (const unsigned char *)data;
    while (size > 0) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += (size_t)written;
        size -= (size_t)written;
    }
    return 0;
}

static void relay_disconnect_locked(relay_state_t *relay) {
    if (relay->client_fd >= 0) {
        close(relay->client_fd);
        relay->client_fd = -1;
    }
    relay->packet_size = 0;
    relay->packet_offset = 0;
}

static int relay_flush_locked(relay_state_t *relay) {
    while (relay->client_fd >= 0 && relay->packet_offset < relay->packet_size) {
        ssize_t sent = send(relay->client_fd,
                            relay->packet + relay->packet_offset,
                            relay->packet_size - relay->packet_offset,
                            MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            relay->packet_offset += (size_t)sent;
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 1;
        }
        relay_disconnect_locked(relay);
        return -1;
    }

    if (relay->packet_size != 0 &&
        relay->packet_offset == relay->packet_size) {
        relay->packet_size = 0;
        relay->packet_offset = 0;
        atomic_fetch_add_explicit(&relay->frames_sent, 1,
                                  memory_order_relaxed);
    }
    return 0;
}

static int relay_accept_locked(relay_state_t *relay) {
    if (relay->client_fd >= 0) {
        return 0;
    }

    int client = accept(relay->listen_fd, NULL, NULL);
    if (client < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        fprintf(stderr, "[camhal] relay accept failed: %s\n",
                strerror(errno));
        return -1;
    }

    int descriptor_flags = fcntl(client, F_GETFD);
    int status_flags = fcntl(client, F_GETFL);
    if (descriptor_flags < 0 || status_flags < 0 ||
        fcntl(client, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0 ||
        fcntl(client, F_SETFL, status_flags | O_NONBLOCK) < 0) {
        fprintf(stderr, "[camhal] relay client setup failed: %s\n",
                strerror(errno));
        close(client);
        return -1;
    }

    int send_buffer = 128 * 1024;
    (void)setsockopt(client, SOL_SOCKET, SO_SNDBUF, &send_buffer,
                     sizeof(send_buffer));
    relay->client_fd = client;
    fprintf(stderr, "[camhal] relay client connected\n");
    return 0;
}

static int relay_open(relay_state_t *relay, const char *socket_path,
                      size_t payload_bytes, unsigned int frame_limit) {
    memset(relay, 0, sizeof(*relay));
    relay->listen_fd = -1;
    relay->client_fd = -1;
    relay->frame_limit = frame_limit;
    atomic_init(&relay->frames_sent, 0);
    atomic_init(&relay->frames_dropped, 0);
    if (pthread_mutex_init(&relay->lock, NULL) != 0) {
        fprintf(stderr, "[camhal] relay mutex initialization failed\n");
        return -1;
    }

    size_t path_length = strlen(socket_path);
    if (path_length == 0 || path_length >= sizeof(relay->socket_path)) {
        fprintf(stderr, "[camhal] relay socket path is invalid or too long\n");
        pthread_mutex_destroy(&relay->lock);
        return -1;
    }
    memcpy(relay->socket_path, socket_path, path_length + 1);

    relay->packet_capacity = sizeof(relay_frame_header_t) + payload_bytes;
    relay->packet = malloc(relay->packet_capacity);
    if (!relay->packet) {
        fprintf(stderr, "[camhal] relay packet allocation failed\n");
        pthread_mutex_destroy(&relay->lock);
        return -1;
    }

    relay->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                              0);
    if (relay->listen_fd < 0) {
        fprintf(stderr, "[camhal] relay socket creation failed: %s\n",
                strerror(errno));
        free(relay->packet);
        pthread_mutex_destroy(&relay->lock);
        return -1;
    }

    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, socket_path, path_length + 1);
    unlink(socket_path);
    if (bind(relay->listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(relay->listen_fd, 1) < 0) {
        fprintf(stderr, "[camhal] relay listen setup failed: %s\n",
                strerror(errno));
        close(relay->listen_fd);
        unlink(socket_path);
        free(relay->packet);
        pthread_mutex_destroy(&relay->lock);
        return -1;
    }
    chmod(socket_path, 0600);
    fprintf(stderr, "[camhal] relay listening on %s\n", socket_path);
    return 0;
}

static void relay_publish(relay_state_t *relay, const unsigned char *frame,
                          size_t frame_size) {
    pthread_mutex_lock(&relay->lock);
    int accept_result = relay_accept_locked(relay);
    if (accept_result != 0) {
        atomic_fetch_add_explicit(&relay->frames_dropped, 1,
                                  memory_order_relaxed);
        pthread_mutex_unlock(&relay->lock);
        return;
    }

    if (relay->packet_size != 0 && relay_flush_locked(relay) != 0) {
        atomic_fetch_add_explicit(&relay->frames_dropped, 1,
                                  memory_order_relaxed);
        pthread_mutex_unlock(&relay->lock);
        return;
    }
    if (atomic_load_explicit(&relay->frames_sent, memory_order_relaxed) >=
        relay->frame_limit) {
        pthread_mutex_unlock(&relay->lock);
        return;
    }
    if (frame_size + sizeof(relay_frame_header_t) > relay->packet_capacity) {
        atomic_fetch_add_explicit(&relay->frames_dropped, 1,
                                  memory_order_relaxed);
        pthread_mutex_unlock(&relay->lock);
        return;
    }

    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    relay_frame_header_t header = {
        .magic = RELAY_MAGIC,
        .version = 1,
        .header_bytes = sizeof(relay_frame_header_t),
        .sequence = ++relay->sequence,
        .timestamp_ns = (uint64_t)now.tv_sec * 1000000000ull +
                        (uint64_t)now.tv_nsec,
        .width = PREVIEW_WIDTH,
        .height = PREVIEW_HEIGHT,
        .pixel_format = RELAY_PIXEL_FORMAT_NV21,
        .payload_bytes = (uint32_t)frame_size,
        .dropped_frames = atomic_load_explicit(&relay->frames_dropped,
                                                memory_order_relaxed),
    };
    memcpy(relay->packet, &header, sizeof(header));
    memcpy(relay->packet + sizeof(header), frame, frame_size);
    relay->packet_size = sizeof(header) + frame_size;
    relay->packet_offset = 0;
    (void)relay_flush_locked(relay);
    pthread_mutex_unlock(&relay->lock);
}

static void relay_close(relay_state_t *relay) {
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
    for (unsigned int attempt = 0; attempt < 200; ++attempt) {
        pthread_mutex_lock(&relay->lock);
        int pending = relay->packet_size != 0;
        int flush_result = pending ? relay_flush_locked(relay) : 0;
        pthread_mutex_unlock(&relay->lock);
        if (!pending || flush_result <= 0) {
            break;
        }
        nanosleep(&delay, NULL);
    }

    pthread_mutex_lock(&relay->lock);
    relay_disconnect_locked(relay);
    if (relay->listen_fd >= 0) {
        close(relay->listen_fd);
        relay->listen_fd = -1;
    }
    unlink(relay->socket_path);
    free(relay->packet);
    relay->packet = NULL;
    pthread_mutex_unlock(&relay->lock);
    pthread_mutex_destroy(&relay->lock);
}
static void camera_data(int32_t message_type, const camera_memory_t *memory,
                        unsigned int index,
                        camera_frame_metadata_t *metadata, void *user) {
    (void)metadata;
    capture_state_t *capture = (capture_state_t *)user;
    unsigned int callback_number =
        atomic_fetch_add_explicit(&capture->callbacks_received, 1,
                                  memory_order_relaxed) + 1;
    if (callback_number <= 5) {
        fprintf(stderr,
                "[camhal] data callback #%u type=0x%x memory=%p data=%p size=%zu index=%u\n",
                callback_number, message_type, (const void *)memory,
                memory ? memory->data : NULL, memory ? memory->size : 0,
                index);
    }

    if ((message_type & CAMERA_MSG_PREVIEW_FRAME) == 0 || !memory ||
        !memory->data) {
        return;
    }

    host_memory_t *host_memory = (host_memory_t *)memory->handle;
    size_t slot_size = memory->size;
    const unsigned char *frame = (const unsigned char *)memory->data;
    if (host_memory && host_memory->mapping == memory->data &&
        index < host_memory->slot_count) {
        slot_size = host_memory->slot_size;
        frame += slot_size * index;
    } else if (index != 0) {
        fprintf(stderr,
                "[camhal] cannot resolve callback buffer index %u safely\n",
                index);
        atomic_store_explicit(&capture->frame_status, -1,
                              memory_order_release);
        return;
    }

    size_t output_size = capture->expected_frame_size;
    if (slot_size < output_size) {
        fprintf(stderr,
                "[camhal] callback slot is smaller than expected frame: %zu < %zu\n",
                slot_size, output_size);
        atomic_store_explicit(&capture->frame_status, -1,
                              memory_order_release);
        return;
    }

    const size_t luma_size = PREVIEW_WIDTH * PREVIEW_HEIGHT;
    unsigned int luma_min = 255;
    unsigned int luma_max = 0;
    uint64_t luma_sum = 0;
    for (size_t position = 0; position < luma_size; ++position) {
        unsigned int value = frame[position];
        if (value < luma_min) {
            luma_min = value;
        }
        if (value > luma_max) {
            luma_max = value;
        }
        luma_sum += value;
    }
    if (callback_number <= capture->minimum_callback ||
        callback_number % 30 == 0) {
        fprintf(stderr,
                "[camhal] callback #%u luma min=%u max=%u mean=%.2f\n",
                callback_number, luma_min, luma_max,
                (double)luma_sum / (double)luma_size);
    }

    if (callback_number < capture->minimum_callback) {
        return;
    }
    if (capture->relay) {
        relay_publish(capture->relay, frame, output_size);
        return;
    }

    int expected_status = 0;
    if (!atomic_compare_exchange_strong_explicit(
            &capture->frame_status, &expected_status, 1,
            memory_order_acq_rel, memory_order_acquire)) {
        return;
    }

    int output = open(capture->output_path,
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (output < 0) {
        fprintf(stderr, "[camhal] open frame output failed: %s\n",
                strerror(errno));
        atomic_store_explicit(&capture->frame_status, -1,
                              memory_order_release);
        return;
    }

    int write_result = write_all(output, frame, output_size);
    int saved_errno = errno;
    close(output);
    if (write_result != 0) {
        fprintf(stderr, "[camhal] writing frame failed: %s\n",
                strerror(saved_errno));
        atomic_store_explicit(&capture->frame_status, -1,
                              memory_order_release);
        return;
    }

    fprintf(stderr, "[camhal] saved %zu-byte preview frame to %s\n",
            output_size, capture->output_path);
    atomic_store_explicit(&capture->frame_status, 2, memory_order_release);
}

static void camera_data_timestamp(int64_t timestamp, int32_t message_type,
                                  const camera_memory_t *memory,
                                  unsigned int index, void *user) {
    (void)memory;
    (void)user;
    fprintf(stderr,
            "[camhal] timestamp callback type=0x%x timestamp=%lld index=%u\n",
            message_type, (long long)timestamp, index);
}

static void finish_without_unload(int status) {
    /* Android 11 aborts while unloading this Android 7 HAL. Process exit
     * releases mappings and file descriptors without running incompatible
     * shared-library destructors. */
    fflush(NULL);
    _exit(status);
}

static int load_module(host_state_t *state, const char *path) {
    fprintf(stderr, "[camhal] dlopen %s\n", path);
    state->dso = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!state->dso) {
        fprintf(stderr, "[camhal] dlopen failed: %s\n", dlerror());
        return 10;
    }

    dlerror();
    state->module = (camera_module_t *)dlsym(state->dso, "HMI");
    const char *error = dlerror();
    if (error || !state->module) {
        fprintf(stderr, "[camhal] dlsym(HMI) failed: %s\n",
                error ? error : "null module");
        return 11;
    }

    camera_module_t *module = state->module;
    fprintf(stderr,
            "[camhal] module id=%s name=%s author=%s module_api=0x%04x hal_api=0x%04x\n",
            module->common.id ? module->common.id : "<null>",
            module->common.name ? module->common.name : "<null>",
            module->common.author ? module->common.author : "<null>",
            module->common.module_api_version,
            module->common.hal_api_version);
    fprintf(stderr,
            "[camhal] module methods=%p init=%p enumerate=%p info=%p callbacks=%p\n",
            (void *)module->common.methods, (void *)module->init,
            (void *)module->get_number_of_cameras,
            (void *)module->get_camera_info, (void *)module->set_callbacks);

    return 0;
}

static int initialize_and_enumerate(host_state_t *state) {
    camera_module_t *module = state->module;

    if (module->init) {
        fprintf(stderr, "[camhal] calling module init\n");
        int init_result = module->init();
        fprintf(stderr, "[camhal] module init result=%d (%s)\n", init_result,
                init_result == 0 ? "ok" : strerror(-init_result));
        if (init_result != 0) {
            return 20;
        }
    }

    if (!module->get_number_of_cameras) {
        fprintf(stderr, "[camhal] get_number_of_cameras is null\n");
        return 21;
    }

    fprintf(stderr, "[camhal] calling get_number_of_cameras\n");
    int count = module->get_number_of_cameras();
    fprintf(stderr, "[camhal] camera count=%d\n", count);
    if (count < 1) {
        return 22;
    }

    if (module->set_callbacks) {
        int callback_result = module->set_callbacks(&kModuleCallbacks);
        fprintf(stderr, "[camhal] set_callbacks result=%d\n", callback_result);
        if (callback_result != 0) {
            return 23;
        }
    }

    if (module->get_camera_info) {
        for (int camera_id = 0; camera_id < count; ++camera_id) {
            camera_info_t info = {0};
            int info_result = module->get_camera_info(camera_id, &info);
            fprintf(stderr,
                    "[camhal] camera[%d] info=%d facing=%d orientation=%d device_version=0x%x metadata=%p\n",
                    camera_id, info_result, info.facing, info.orientation,
                    info.device_version,
                    (const void *)info.static_camera_characteristics);
            if (info_result != 0) {
                return 24;
            }
        }
    }

    return 0;
}

static int open_camera(host_state_t *state, const char *camera_id) {
    camera_module_t *module = state->module;
    if (!module->common.methods || !module->common.methods->open) {
        fprintf(stderr, "[camhal] module open method is null\n");
        return 30;
    }

    fprintf(stderr, "[camhal] opening camera %s\n", camera_id);
    hw_device_t *raw_device = NULL;
    int open_result = module->common.methods->open(
        &module->common, camera_id, &raw_device);
    fprintf(stderr, "[camhal] open result=%d raw_device=%p\n",
            open_result, (void *)raw_device);
    if (open_result != 0 || !raw_device) {
        if (open_result < 0) {
            fprintf(stderr, "[camhal] open error=%s\n", strerror(-open_result));
        }
        return 31;
    }

    state->device = (camera_device_t *)raw_device;
    camera_device_t *device = state->device;
    fprintf(stderr,
            "[camhal] device tag=0x%08x version=0x%08x module=%p close=%p ops=%p priv=%p\n",
            device->common.tag, device->common.version,
            (void *)device->common.module, (void *)device->common.close,
            (void *)device->ops, device->priv);

    if (!device->ops) {
        fprintf(stderr, "[camhal] camera device operations are null\n");
        return 32;
    }

    fprintf(stderr,
            "[camhal] ops callbacks=%p preview_window=%p start_preview=%p get_parameters=%p release=%p\n",
            (void *)device->ops->set_callbacks,
            (void *)device->ops->set_preview_window,
            (void *)device->ops->start_preview,
            (void *)device->ops->get_parameters,
            (void *)device->ops->release);
    return 0;
}

static int print_parameters(host_state_t *state) {
    camera_device_t *device = state->device;
    if (!device || !device->ops || !device->ops->get_parameters) {
        fprintf(stderr, "[camhal] get_parameters is unavailable\n");
        return 40;
    }

    fprintf(stderr, "[camhal] calling get_parameters\n");
    char *parameters = device->ops->get_parameters(device);
    if (!parameters) {
        fprintf(stderr, "[camhal] get_parameters returned null\n");
        return 41;
    }

    size_t length = strnlen(parameters, MAX_PARAMETER_BYTES);
    if (length == MAX_PARAMETER_BYTES) {
        fprintf(stderr, "[camhal] parameter string is not terminated within the safety limit\n");
        if (device->ops->put_parameters) {
            device->ops->put_parameters(device, parameters);
        }
        return 42;
    }

    fprintf(stderr, "[camhal] parameters length=%zu\n", length);
    printf("%s\n", parameters);
    fflush(stdout);

    if (device->ops->put_parameters) {
        device->ops->put_parameters(device, parameters);
    } else {
        free(parameters);
    }
    return 0;
}

static int capture_one_frame(host_state_t *state, const char *output_path) {
    camera_device_t *device = state->device;
    if (!device || !device->ops || !device->ops->set_callbacks ||
        !device->ops->enable_msg_type || !device->ops->start_preview ||
        !device->ops->stop_preview) {
        fprintf(stderr, "[camhal] required preview callbacks are unavailable\n");
        return 60;
    }

    capture_state_t capture = {
        .output_path = output_path,
        .expected_frame_size = PREVIEW_BYTES,
        .minimum_callback = 10,
    };
    atomic_init(&capture.callbacks_received, 0);
    atomic_init(&capture.frame_status, 0);

    fprintf(stderr,
            "[camhal] configuring callback-only 640x480 NV21 preview; no preview window\n");
    device->ops->set_callbacks(device, camera_notify, camera_data,
                               camera_data_timestamp, request_camera_memory,
                               &capture);
    device->ops->enable_msg_type(device, CAMERA_MSG_ERROR |
                                         CAMERA_MSG_PREVIEW_FRAME);

    fprintf(stderr, "[camhal] calling start_preview\n");
    int start_result = device->ops->start_preview(device);
    fprintf(stderr, "[camhal] start_preview result=%d\n", start_result);
    if (start_result != 0) {
        device->ops->disable_msg_type(device, CAMERA_MSG_PREVIEW_FRAME);
        return 61;
    }

    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
    for (unsigned int iteration = 0; iteration < 800; ++iteration) {
        int frame_status = atomic_load_explicit(&capture.frame_status,
                                                memory_order_acquire);
        if (frame_status == 2 || frame_status < 0) {
            break;
        }
        nanosleep(&delay, NULL);
    }

    fprintf(stderr, "[camhal] calling stop_preview\n");
    device->ops->stop_preview(device);
    device->ops->disable_msg_type(device, CAMERA_MSG_PREVIEW_FRAME |
                                          CAMERA_MSG_ERROR);
    fprintf(stderr,
            "[camhal] preview stopped callbacks=%u frame_status=%d enabled=%d\n",
            atomic_load_explicit(&capture.callbacks_received,
                                 memory_order_relaxed),
            atomic_load_explicit(&capture.frame_status, memory_order_acquire),
            device->ops->preview_enabled ?
                device->ops->preview_enabled(device) : -1);

    return atomic_load_explicit(&capture.frame_status, memory_order_acquire) == 2
               ? 0
               : 62;
}

static int relay_preview_frames(host_state_t *state, const char *socket_path,
                                unsigned int duration_seconds,
                                unsigned int frame_limit) {
    camera_device_t *device = state->device;
    if (!device || !device->ops || !device->ops->set_callbacks ||
        !device->ops->enable_msg_type || !device->ops->start_preview ||
        !device->ops->stop_preview) {
        fprintf(stderr, "[camhal] required preview callbacks are unavailable\n");
        return 63;
    }

    relay_state_t relay;
    if (relay_open(&relay, socket_path, PREVIEW_BYTES, frame_limit) != 0) {
        return 64;
    }
    capture_state_t capture = {
        .relay = &relay,
        .expected_frame_size = PREVIEW_BYTES,
        .minimum_callback = 5,
    };
    atomic_init(&capture.callbacks_received, 0);
    atomic_init(&capture.frame_status, 0);

    fprintf(stderr,
            "[camhal] configuring bounded 640x480 NV21 relay; duration=%us frames=%u\n",
            duration_seconds, frame_limit);
    device->ops->set_callbacks(device, camera_notify, camera_data,
                               camera_data_timestamp, request_camera_memory,
                               &capture);
    device->ops->enable_msg_type(device, CAMERA_MSG_ERROR |
                                         CAMERA_MSG_PREVIEW_FRAME);

    fprintf(stderr, "[camhal] calling start_preview\n");
    int start_result = device->ops->start_preview(device);
    fprintf(stderr, "[camhal] start_preview result=%d\n", start_result);
    if (start_result != 0) {
        device->ops->disable_msg_type(device, CAMERA_MSG_PREVIEW_FRAME |
                                              CAMERA_MSG_ERROR);
        relay_close(&relay);
        return 65;
    }

    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
    unsigned int iterations = duration_seconds * 100u;
    for (unsigned int iteration = 0; iteration < iterations; ++iteration) {
        if (atomic_load_explicit(&relay.frames_sent, memory_order_acquire) >=
                frame_limit ||
            atomic_load_explicit(&capture.frame_status, memory_order_acquire) <
                0) {
            break;
        }
        nanosleep(&delay, NULL);
    }

    fprintf(stderr, "[camhal] calling stop_preview\n");
    device->ops->stop_preview(device);
    device->ops->disable_msg_type(device, CAMERA_MSG_PREVIEW_FRAME |
                                          CAMERA_MSG_ERROR);
    relay_close(&relay);
    unsigned int sent = atomic_load_explicit(&relay.frames_sent,
                                              memory_order_acquire);
    unsigned int dropped = atomic_load_explicit(&relay.frames_dropped,
                                                 memory_order_acquire);
    fprintf(stderr,
            "[camhal] relay stopped callbacks=%u sent=%u dropped=%u enabled=%d\n",
            atomic_load_explicit(&capture.callbacks_received,
                                 memory_order_relaxed),
            sent, dropped,
            device->ops->preview_enabled ?
                device->ops->preview_enabled(device) : -1);

    return sent > 0 &&
                   atomic_load_explicit(&capture.frame_status,
                                        memory_order_acquire) >= 0
               ? 0
               : 66;
}

static int close_camera(host_state_t *state) {
    camera_device_t *device = state->device;
    if (!device) {
        return 0;
    }

    if (device->ops && device->ops->release) {
        fprintf(stderr, "[camhal] calling camera release\n");
        device->ops->release(device);
        fprintf(stderr, "[camhal] camera release returned\n");
    }

    if (device->common.close) {
        fprintf(stderr, "[camhal] calling camera close\n");
        int close_result = device->common.close(&device->common);
        fprintf(stderr, "[camhal] camera close result=%d\n", close_result);
        state->device = NULL;
        return close_result == 0 ? 0 : 51;
    }

    fprintf(stderr, "[camhal] camera close method is null\n");
    return 50;
}

static unsigned int bounded_environment_value(const char *name,
                                              unsigned int fallback,
                                              unsigned int maximum) {
    const char *text = getenv(name);
    if (!text || !*text) {
        return fallback;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0 ||
        value > maximum) {
        fprintf(stderr,
                "[camhal] ignoring invalid %s=%s; expected 1..%u\n",
                name, text, maximum);
        return fallback;
    }
    return (unsigned int)value;
}
static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s enumerate|open|parameters|capture-one|relay|close "
            "[camera-id] [hal-path]\n",
            program);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    const char *mode = argv[1];
    const char *camera_id = argc > 2 ? argv[2] : "0";
    const char *hal_path = argc > 3 ? argv[3] : getenv("CAMERA_HAL_PATH");
    if (!hal_path || !*hal_path) {
        hal_path = DEFAULT_HAL_PATH;
    }

    host_state_t state = {0};
    int result = load_module(&state, hal_path);
    if (result == 0) {
        result = initialize_and_enumerate(&state);
    }

    if (result == 0 && strcmp(mode, "enumerate") != 0) {
        result = open_camera(&state, camera_id);
    }
    if (result == 0 && strcmp(mode, "parameters") == 0) {
        result = print_parameters(&state);
    }
    if (result == 0 && strcmp(mode, "capture-one") == 0) {
        const char *output_path = getenv("CAMHAL_FRAME_PATH");
        if (!output_path || !*output_path) {
            output_path = "/data/local/tmp/camprobe/frame-640x480.nv21";
        }
        result = capture_one_frame(&state, output_path);
    }
    if (result == 0 && strcmp(mode, "relay") == 0) {
        const char *socket_path = getenv("CAMHAL_RELAY_SOCKET");
        if (!socket_path || !*socket_path) {
            socket_path = DEFAULT_RELAY_SOCKET;
        }
        unsigned int duration = bounded_environment_value(
            "CAMHAL_RELAY_SECONDS", 20, 300);
        unsigned int frames = bounded_environment_value(
            "CAMHAL_RELAY_FRAMES", 120, 10000);
        result = relay_preview_frames(&state, socket_path, duration, frames);
    }
    if (result == 0 && strcmp(mode, "close") == 0) {
        result = close_camera(&state);
    }
    if (strcmp(mode, "enumerate") != 0 && strcmp(mode, "open") != 0 &&
        strcmp(mode, "parameters") != 0 &&
        strcmp(mode, "capture-one") != 0 && strcmp(mode, "relay") != 0 &&
        strcmp(mode, "close") != 0) {
        usage(argv[0]);
        result = 2;
    }

    fprintf(stderr, "[camhal] mode=%s result=%d; exiting without dlclose\n",
            mode, result);
    finish_without_unload(result);
}
