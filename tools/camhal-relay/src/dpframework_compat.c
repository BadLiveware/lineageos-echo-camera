#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

/*
 * Android 7 camera objects embed DpIspStream and DpBlitStream using their old
 * class sizes, while Android 9's kernel-compatible libdpframework expects much
 * larger objects. Calling the newer constructor on the old allocation writes
 * beyond it. Route every Dp method used by the camera blobs to separately
 * allocated Android 9 objects held in an external registry; the old blobs also
 * write their own fields, so the association cannot live inside the object.
 */

#define DP_PROXY_BYTES (64u * 1024u)
#define DP_PROXY_CAPACITY 32u
#define DP_SECURE_NONE 0

typedef int32_t dp_status_t;

/* Android 9 repacked DpColorFormat's video/plane/subsampling fields. */
static int32_t translate_color(int32_t legacy_color) {
    uint32_t legacy = (uint32_t)legacy_color;
    uint32_t video = (legacy >> 27) & 0x7u;
    uint32_t planes = (legacy >> 24) & 0x7u;
    uint32_t horizontal = (legacy >> 20) & 0x3u;
    uint32_t vertical = (legacy >> 18) & 0x3u;
    uint32_t translated = (video << 23) | (planes << 21) |
                          (horizontal << 19) | ((vertical & 0x1u) << 18) |
                          (legacy & 0x3ffffu);

    uint32_t group = (legacy >> 6) & 0x3u;
    uint32_t unique = legacy & 0x1fu;
    if (group == 2u && unique >= 20u && unique <= 22u) {
        translated = (translated & ~0x1fu) | 20u;
    } else if (group == 3u) {
        translated = (translated & ~(0x3u << 6)) | (2u << 6);
        if (unique >= 20u && unique <= 22u) {
            translated = (translated & ~0x1fu) | 21u;
        }
    }
    return (int32_t)translated;
}

typedef struct proxy_entry {
    void *legacy;
    void *current;
} proxy_entry_t;

static pthread_mutex_t g_proxy_lock = PTHREAD_MUTEX_INITIALIZER;
static proxy_entry_t g_proxies[DP_PROXY_CAPACITY];

static void register_proxy(void *legacy, void *current) {
    size_t available = DP_PROXY_CAPACITY;
    pthread_mutex_lock(&g_proxy_lock);
    for (size_t index = 0; index < DP_PROXY_CAPACITY; ++index) {
        if (g_proxies[index].legacy == legacy) {
            g_proxies[index].current = current;
            pthread_mutex_unlock(&g_proxy_lock);
            return;
        }
        if (!g_proxies[index].legacy && available == DP_PROXY_CAPACITY) {
            available = index;
        }
    }
    if (available < DP_PROXY_CAPACITY) {
        g_proxies[available].legacy = legacy;
        g_proxies[available].current = current;
    }
    pthread_mutex_unlock(&g_proxy_lock);
    if (available == DP_PROXY_CAPACITY) {
        __android_log_print(ANDROID_LOG_ERROR, "CheckersDpCompat",
                            "Dp proxy registry is full");
    }
}

static void *proxy_object(void *legacy) {
    void *current = NULL;
    pthread_mutex_lock(&g_proxy_lock);
    for (size_t index = 0; index < DP_PROXY_CAPACITY; ++index) {
        if (g_proxies[index].legacy == legacy) {
            current = g_proxies[index].current;
            break;
        }
    }
    pthread_mutex_unlock(&g_proxy_lock);
    return current;
}

static void *unregister_proxy(void *legacy) {
    void *current = NULL;
    pthread_mutex_lock(&g_proxy_lock);
    for (size_t index = 0; index < DP_PROXY_CAPACITY; ++index) {
        if (g_proxies[index].legacy == legacy) {
            current = g_proxies[index].current;
            g_proxies[index].legacy = NULL;
            g_proxies[index].current = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_proxy_lock);
    return current;
}

static void *resolve_next(const char *symbol) {
    void *function = dlsym(RTLD_NEXT, symbol);
    if (!function) {
        __android_log_print(ANDROID_LOG_ERROR, "CheckersDpCompat",
                            "missing next symbol %s: %s", symbol, dlerror());
    }
    return function;
}

static void construct_proxy(void *legacy_object, const char *constructor,
                            int has_argument, int32_t argument) {
    if (!legacy_object) {
        return;
    }
    void *object = calloc(1, DP_PROXY_BYTES);
    if (!object) {
        __android_log_print(ANDROID_LOG_ERROR, "CheckersDpCompat",
                            "unable to allocate Dp proxy object");
        return;
    }

    if (has_argument) {
        typedef void (*constructor_fn)(void *, int32_t);
        constructor_fn function = (constructor_fn)resolve_next(constructor);
        if (!function) {
            free(object);
            return;
        }
        function(object, argument);
    } else {
        typedef void (*constructor_fn)(void *);
        constructor_fn function = (constructor_fn)resolve_next(constructor);
        if (!function) {
            free(object);
            return;
        }
        function(object);
    }

    register_proxy(legacy_object, object);
    __android_log_print(ANDROID_LOG_INFO, "CheckersDpCompat",
                        "constructed proxy legacy=%p current=%p type=%d",
                        legacy_object, object, argument);
}

static void destroy_proxy(void *legacy_object, const char *destructor) {
    void *object = unregister_proxy(legacy_object);
    if (!object) {
        return;
    }
    typedef void (*destructor_fn)(void *);
    destructor_fn function = (destructor_fn)resolve_next(destructor);
    if (function) {
        function(object);
    }
    free(object);
}

#define REQUIRE_PROXY(legacy)                                      \
    void *current = proxy_object(legacy);                          \
    if (!current) {                                                \
        __android_log_print(ANDROID_LOG_ERROR, "CheckersDpCompat", \
                            "%s missing proxy for legacy object %p", \
                            __func__, (void *)(legacy));            \
        return -1;                                                  \
    }

#define RESOLVE_AND_CALL(symbol, type, ...)                            \
    do {                                                               \
        type function = (type)resolve_next(symbol);                    \
        if (!function)                                                 \
            return -1;                                                 \
        dp_status_t call_result = function(__VA_ARGS__);               \
        if (call_result != 0)                                          \
            __android_log_print(ANDROID_LOG_ERROR, "CheckersDpCompat", \
                                "%s returned %d", __func__,           \
                                call_result);                           \
        return call_result;                                            \
    } while (0)

__attribute__((visibility("default")))
void *isp_ctor(void *stream, int32_t stream_type)
    __asm__("_ZN11DpIspStreamC1ENS_13ISPStreamTypeE");
void *isp_ctor(void *stream, int32_t stream_type) {
    construct_proxy(stream, "_ZN11DpIspStreamC1ENS_13ISPStreamTypeE", 1,
                    stream_type);
    return stream;
}

__attribute__((visibility("default")))
void isp_dtor(void *stream) __asm__("_ZN11DpIspStreamD1Ev");
void isp_dtor(void *stream) {
    destroy_proxy(stream, "_ZN11DpIspStreamD1Ev");
}

__attribute__((visibility("default")))
dp_status_t isp_set_src_crop(void *stream, int32_t x, int32_t sub_x,
                             int32_t y, int32_t sub_y, int32_t width,
                             int32_t height)
    __asm__("_ZN11DpIspStream10setSrcCropEiiiiii");
dp_status_t isp_set_src_crop(void *stream, int32_t x, int32_t sub_x,
                             int32_t y, int32_t sub_y, int32_t width,
                             int32_t height) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, int32_t, int32_t, int32_t,
                              int32_t, int32_t);
    RESOLVE_AND_CALL("_ZN11DpIspStream10setSrcCropEiiiiii", fn, current, x,
                     sub_x, y, sub_y, width, height);
}

__attribute__((visibility("default")))
dp_status_t isp_stop(void *stream) __asm__("_ZN11DpIspStream10stopStreamEv");
dp_status_t isp_stop(void *stream) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *);
    RESOLVE_AND_CALL("_ZN11DpIspStream10stopStreamEv", fn, current);
}

__attribute__((visibility("default")))
dp_status_t isp_rotation(void *stream, int32_t port, int32_t rotation)
    __asm__("_ZN11DpIspStream11setRotationEii");
dp_status_t isp_rotation(void *stream, int32_t port, int32_t rotation) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, int32_t);
    RESOLVE_AND_CALL("_ZN11DpIspStream11setRotationEii", fn, current, port,
                     rotation);
}

__attribute__((visibility("default")))
dp_status_t isp_start(void *stream) __asm__("_ZN11DpIspStream11startStreamEv");
dp_status_t isp_start(void *stream) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, struct timeval *);
    struct timeval timing = {0};
    RESOLVE_AND_CALL("_ZN11DpIspStream11startStreamEP7timeval", fn, current,
                     &timing);
}

__attribute__((visibility("default")))
dp_status_t isp_set_dst(void *stream, int32_t port, int32_t width,
                        int32_t height, int32_t y_pitch, int32_t uv_pitch,
                        int32_t color, int32_t profile, int32_t field,
                        void *roi, bool do_flush)
    __asm__("_ZN11DpIspStream12setDstConfigEiiiii13DP_COLOR_ENUM15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRectb");
dp_status_t isp_set_dst(void *stream, int32_t port, int32_t width,
                        int32_t height, int32_t y_pitch, int32_t uv_pitch,
                        int32_t color, int32_t profile, int32_t field,
                        void *roi, bool do_flush) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, int32_t, int32_t, int32_t,
                              int32_t, int32_t, int32_t, int32_t, void *, bool,
                              int32_t);
    RESOLVE_AND_CALL(
        "_ZN11DpIspStream12setDstConfigEiiiii13DP_COLOR_ENUM15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRectb8DpSecure",
        fn, current, port, width, height, y_pitch, uv_pitch,
        translate_color(color), profile, field, roi, do_flush, DP_SECURE_NONE);
}

__attribute__((visibility("default")))
dp_status_t isp_set_parameter(void *stream, void *config, uint32_t hint)
    __asm__("_ZN11DpIspStream12setParameterER23ISP_TPIPE_CONFIG_STRUCTj");
dp_status_t isp_set_parameter(void *stream, void *config, uint32_t hint) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void *, uint32_t);
    RESOLVE_AND_CALL(
        "_ZN11DpIspStream12setParameterER23ISP_TPIPE_CONFIG_STRUCTj", fn,
        current, config, hint);
}

__attribute__((visibility("default")))
dp_status_t isp_set_src_short(void *stream, int32_t color, int32_t width,
                              int32_t height, int32_t pitch, bool do_flush)
    __asm__("_ZN11DpIspStream12setSrcConfigE13DP_COLOR_ENUMiiib");
dp_status_t isp_set_src_short(void *stream, int32_t color, int32_t width,
                              int32_t height, int32_t pitch, bool do_flush) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, int32_t, int32_t, int32_t,
                              bool);
    RESOLVE_AND_CALL("_ZN11DpIspStream12setSrcConfigE13DP_COLOR_ENUMiiib", fn,
                     current, translate_color(color), width, height, pitch,
                     do_flush);
}

__attribute__((visibility("default")))
dp_status_t isp_set_src(void *stream, int32_t width, int32_t height,
                        int32_t y_pitch, int32_t uv_pitch, int32_t color,
                        int32_t profile, int32_t field, void *roi,
                        bool do_flush)
    __asm__("_ZN11DpIspStream12setSrcConfigEiiii13DP_COLOR_ENUM15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRectb");
dp_status_t isp_set_src(void *stream, int32_t width, int32_t height,
                        int32_t y_pitch, int32_t uv_pitch, int32_t color,
                        int32_t profile, int32_t field, void *roi,
                        bool do_flush) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, int32_t, int32_t, int32_t,
                              int32_t, int32_t, int32_t, void *, bool,
                              int32_t);
    RESOLVE_AND_CALL(
        "_ZN11DpIspStream12setSrcConfigEiiii13DP_COLOR_ENUM15DP_PROFILE_ENUM17DpInterlaceFormatP6DpRectb8DpSecure",
        fn, current, width, height, y_pitch, uv_pitch, translate_color(color),
        profile, field, roi, do_flush, DP_SECURE_NONE);
}

__attribute__((visibility("default")))
dp_status_t isp_flip(void *stream, int32_t port, bool flip)
    __asm__("_ZN11DpIspStream13setFlipStatusEib");
dp_status_t isp_flip(void *stream, int32_t port, bool flip) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, bool);
    RESOLVE_AND_CALL("_ZN11DpIspStream13setFlipStatusEib", fn, current, port,
                     flip);
}

__attribute__((visibility("default")))
dp_status_t isp_queue_dst(void *stream, int32_t port, void **addresses,
                          uint32_t *mvas, uint32_t *sizes, int32_t planes)
    __asm__("_ZN11DpIspStream14queueDstBufferEiPPvPjS2_i");
dp_status_t isp_queue_dst(void *stream, int32_t port, void **addresses,
                          uint32_t *mvas, uint32_t *sizes, int32_t planes) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, void **, uint32_t *, uint32_t *,
                              int32_t);
    RESOLVE_AND_CALL("_ZN11DpIspStream14queueDstBufferEiPPvPjS2_i", fn,
                     current, port, addresses, mvas, sizes, planes);
}

__attribute__((visibility("default")))
dp_status_t isp_queue_src_planes(void *stream, void **addresses,
                                 uint32_t *mvas, uint32_t *sizes,
                                 int32_t planes)
    __asm__("_ZN11DpIspStream14queueSrcBufferEPPvPjS2_i");
dp_status_t isp_queue_src_planes(void *stream, void **addresses,
                                 uint32_t *mvas, uint32_t *sizes,
                                 int32_t planes) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void **, uint32_t *, uint32_t *, int32_t);
    RESOLVE_AND_CALL("_ZN11DpIspStream14queueSrcBufferEPPvPjS2_i", fn,
                     current, addresses, mvas, sizes, planes);
}

__attribute__((visibility("default")))
dp_status_t isp_queue_src(void *stream, void *address, uint32_t mva,
                          uint32_t size)
    __asm__("_ZN11DpIspStream14queueSrcBufferEPvjj");
dp_status_t isp_queue_src(void *stream, void *address, uint32_t mva,
                          uint32_t size) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void *, uint32_t, uint32_t);
    RESOLVE_AND_CALL("_ZN11DpIspStream14queueSrcBufferEPvjj", fn, current,
                     address, mva, size);
}

__attribute__((visibility("default")))
dp_status_t isp_dequeue_end(void *stream, uint32_t *job_id)
    __asm__("_ZN11DpIspStream15dequeueFrameEndEPj");
dp_status_t isp_dequeue_end(void *stream, uint32_t *job_id) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, uint32_t *);
    RESOLVE_AND_CALL("_ZN11DpIspStream15dequeueFrameEndEPj", fn, current,
                     job_id);
}

__attribute__((visibility("default")))
dp_status_t isp_dequeue_dst(void *stream, int32_t port, void **address,
                            bool wait)
    __asm__("_ZN11DpIspStream16dequeueDstBufferEiPPvb");
dp_status_t isp_dequeue_dst(void *stream, int32_t port, void **address,
                            bool wait) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, void **, bool);
    RESOLVE_AND_CALL("_ZN11DpIspStream16dequeueDstBufferEiPPvb", fn, current,
                     port, address, wait);
}

__attribute__((visibility("default")))
dp_status_t isp_dequeue_src(void *stream)
    __asm__("_ZN11DpIspStream16dequeueSrcBufferEv");
dp_status_t isp_dequeue_src(void *stream) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *);
    RESOLVE_AND_CALL("_ZN11DpIspStream16dequeueSrcBufferEv", fn, current);
}

__attribute__((visibility("default")))
void *blit_ctor(void *stream) __asm__("_ZN12DpBlitStreamC1Ev");
void *blit_ctor(void *stream) {
    construct_proxy(stream, "_ZN12DpBlitStreamC1Ev", 0, 0);
    return stream;
}

__attribute__((visibility("default")))
void blit_dtor(void *stream) __asm__("_ZN12DpBlitStreamD1Ev");
void blit_dtor(void *stream) {
    destroy_proxy(stream, "_ZN12DpBlitStreamD1Ev");
}

__attribute__((visibility("default")))
dp_status_t blit_invalidate(void *stream)
    __asm__("_ZN12DpBlitStream10invalidateEv");
dp_status_t blit_invalidate(void *stream) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void *);
    RESOLVE_AND_CALL("_ZN12DpBlitStream10invalidateEP7timeval", fn, current,
                     NULL);
}

#define DEFINE_BLIT_BUFFER_WRAPPER(c_name, symbol, signature, arguments) \
    __attribute__((visibility("default")))                              \
    dp_status_t c_name signature __asm__(symbol);                        \
    dp_status_t c_name signature {                                      \
        REQUIRE_PROXY(stream);                                          \
        typedef dp_status_t (*fn) signature;                            \
        RESOLVE_AND_CALL(symbol, fn, arguments);                        \
    }

/* These explicit wrappers keep the ABI signatures reviewable. */
__attribute__((visibility("default")))
dp_status_t blit_set_dst_fd(void *stream, int32_t fd, uint32_t *sizes,
                            uint32_t planes)
    __asm__("_ZN12DpBlitStream12setDstBufferEiPjj");
dp_status_t blit_set_dst_fd(void *stream, int32_t fd, uint32_t *sizes,
                            uint32_t planes) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, uint32_t *, uint32_t);
    RESOLVE_AND_CALL("_ZN12DpBlitStream12setDstBufferEiPjj", fn, current, fd,
                     sizes, planes);
}

__attribute__((visibility("default")))
dp_status_t blit_set_dst_planes(void *stream, void **addresses,
                                uint32_t *sizes, uint32_t planes)
    __asm__("_ZN12DpBlitStream12setDstBufferEPPvPjj");
dp_status_t blit_set_dst_planes(void *stream, void **addresses,
                                uint32_t *sizes, uint32_t planes) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void **, uint32_t *, uint32_t);
    RESOLVE_AND_CALL("_ZN12DpBlitStream12setDstBufferEPPvPjj", fn, current,
                     addresses, sizes, planes);
}

__attribute__((visibility("default")))
dp_status_t blit_set_dst(void *stream, void *address, uint32_t size)
    __asm__("_ZN12DpBlitStream12setDstBufferEPvj");
dp_status_t blit_set_dst(void *stream, void *address, uint32_t size) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void *, uint32_t);
    RESOLVE_AND_CALL("_ZN12DpBlitStream12setDstBufferEPvj", fn, current,
                     address, size);
}

__attribute__((visibility("default")))
dp_status_t blit_set_dst_config(void *stream, int32_t width, int32_t height,
                                int32_t color, int32_t field, void *roi)
    __asm__("_ZN12DpBlitStream12setDstConfigEii13DP_COLOR_ENUM17DpInterlaceFormatP6DpRect");
dp_status_t blit_set_dst_config(void *stream, int32_t width, int32_t height,
                                int32_t color, int32_t field, void *roi) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, int32_t, int32_t, int32_t,
                              void *);
    RESOLVE_AND_CALL(
        "_ZN12DpBlitStream12setDstConfigEii13DP_COLOR_ENUM17DpInterlaceFormatP6DpRect",
        fn, current, width, height, translate_color(color), field, roi);
}

__attribute__((visibility("default")))
dp_status_t blit_set_src_fd(void *stream, int32_t fd, uint32_t *sizes,
                            uint32_t planes)
    __asm__("_ZN12DpBlitStream12setSrcBufferEiPjj");
dp_status_t blit_set_src_fd(void *stream, int32_t fd, uint32_t *sizes,
                            uint32_t planes) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, uint32_t *, uint32_t);
    RESOLVE_AND_CALL("_ZN12DpBlitStream12setSrcBufferEiPjj", fn, current, fd,
                     sizes, planes);
}

__attribute__((visibility("default")))
dp_status_t blit_set_src_planes(void *stream, void **addresses,
                                uint32_t *sizes, uint32_t planes)
    __asm__("_ZN12DpBlitStream12setSrcBufferEPPvPjj");
dp_status_t blit_set_src_planes(void *stream, void **addresses,
                                uint32_t *sizes, uint32_t planes) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void **, uint32_t *, uint32_t);
    RESOLVE_AND_CALL("_ZN12DpBlitStream12setSrcBufferEPPvPjj", fn, current,
                     addresses, sizes, planes);
}

__attribute__((visibility("default")))
dp_status_t blit_set_src(void *stream, void *address, uint32_t size)
    __asm__("_ZN12DpBlitStream12setSrcBufferEPvj");
dp_status_t blit_set_src(void *stream, void *address, uint32_t size) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, void *, uint32_t);
    RESOLVE_AND_CALL("_ZN12DpBlitStream12setSrcBufferEPvj", fn, current,
                     address, size);
}

__attribute__((visibility("default")))
dp_status_t blit_set_src_config(void *stream, int32_t width, int32_t height,
                                int32_t color, int32_t field, void *roi)
    __asm__("_ZN12DpBlitStream12setSrcConfigEii13DP_COLOR_ENUM17DpInterlaceFormatP6DpRect");
dp_status_t blit_set_src_config(void *stream, int32_t width, int32_t height,
                                int32_t color, int32_t field, void *roi) {
    REQUIRE_PROXY(stream);
    typedef dp_status_t (*fn)(void *, int32_t, int32_t, int32_t, int32_t,
                              void *);
    RESOLVE_AND_CALL(
        "_ZN12DpBlitStream12setSrcConfigEii13DP_COLOR_ENUM17DpInterlaceFormatP6DpRect",
        fn, current, width, height, translate_color(color), field, roi);
}
