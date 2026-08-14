#include <android/log.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

#define LOG_TAG "CrownImgSensorCompat"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define OLD_INFO_SIZE 144U
#define NEW_INFO_SIZE 216U
#define OLD_INFO2_SIZE 204U
#define NEW_INFO2_SIZE 216U

struct compat_getinfo {
    uint32_t scenario[2];
    uint32_t info[2];
    uint32_t config[2];
};

struct compat_getinfo2 {
    uint32_t sensor_id;
    uint32_t info;
    uint32_t resolution;
};

struct compat_feature {
    uint32_t invoke_camera;
    uint32_t feature_id;
    uint32_t parameter;
    uint32_t parameter_length;
};

struct compat_control {
    uint32_t invoke_camera;
    uint32_t scenario;
    uint32_t image_window;
    uint32_t sensor_config;
};

struct old_mclk {
    uint8_t on;
    uint8_t padding[3];
    uint32_t frequency_group;
};

struct new_mclk {
    uint8_t on;
    uint8_t padding[3];
    uint32_t frequency_group;
    uint8_t timing_generator;
    uint8_t trailing_padding[3];
};

struct field_map {
    uint16_t old_offset;
    uint16_t new_offset;
    uint8_t size;
};

static const struct field_map info_tail_fields[] = {
    {96, 136, 2}, {98, 138, 1}, {99, 139, 1}, {100, 140, 1},
    {101, 141, 1}, {102, 143, 1}, {103, 144, 1}, {104, 150, 1},
    {105, 151, 1}, {106, 152, 1}, {107, 153, 1}, {108, 156, 4},
    {112, 160, 1}, {113, 161, 1}, {114, 162, 1}, {116, 164, 2},
    {120, 168, 4}, {124, 172, 1}, {125, 174, 1}, {126, 180, 1},
    {128, 184, 4}, {132, 188, 1}, {133, 189, 1}, {134, 190, 1},
    {135, 191, 1}, {136, 192, 1}, {140, 196, 4},
};

typedef int (*ioctl_fn)(int, int, ...);

static ioctl_fn next_ioctl;

/* Crown has no usable TSF calibration in the Lineage workspace. The legacy
 * algorithm dereferences a null RFK table after preview starts. These exported
 * MediaTek methods are called through the PLT, so returning false keeps static
 * lens shading while disabling only temporal shading correction. */
int crown_lsc_get_tsf(void *instance)
    __asm__("_ZN11NSIspTuning7LscMgr211getTsfOnOffEv");
int crown_lsc_rto_get_tsf(void *instance)
    __asm__("_ZN11NSIspTuning10LscMgr2Rto11getTsfOnOffEv");

int crown_lsc_get_tsf(void *instance)
{
    (void)instance;
    return 0;
}

int crown_lsc_rto_get_tsf(void *instance)
{
    (void)instance;
    return 0;
}

static uint32_t scenario_to_kernel(uint32_t scenario)
{
    if (scenario <= 3)
        return scenario;
    if (scenario <= 9)
        return scenario + 6;
    if (scenario <= 15)
        return scenario - 6;
    return scenario;
}

static uint32_t feature_to_kernel(uint32_t feature)
{
    if (feature <= 3006)
        return feature;
    if (feature <= 3061)
        return feature + 1;
    if (feature == 3062)
        return 3127; /* legacy SENSOR_FEATURE_GET_SENSOR_ID */
    return feature;
}

static void info_to_legacy(void *legacy, const void *kernel)
{
    size_t i;

    memset(legacy, 0, OLD_INFO_SIZE);
    memcpy(legacy, kernel, 96);
    for (i = 0; i < ARRAY_SIZE(info_tail_fields); ++i) {
        const struct field_map *field = &info_tail_fields[i];
        memcpy((uint8_t *)legacy + field->old_offset,
               (const uint8_t *)kernel + field->new_offset,
               field->size);
    }
}

static void info2_to_legacy(void *legacy, const void *kernel)
{
    memset(legacy, 0, OLD_INFO2_SIZE);
    memcpy(legacy, kernel, 166);
    ((uint8_t *)legacy)[166] = ((const uint8_t *)kernel)[167];
    memcpy((uint8_t *)legacy + 168, (const uint8_t *)kernel + 168,
           OLD_INFO2_SIZE - 168);
}

static int handle_getinfo(int fd, int request, struct compat_getinfo *legacy)
{
    struct compat_getinfo translated = *legacy;
    uint8_t info[2][NEW_INFO_SIZE] = {{0}};
    int ret;
    size_t i;

    for (i = 0; i < 2; ++i) {
        translated.scenario[i] = scenario_to_kernel(legacy->scenario[i]);
        translated.info[i] = (uint32_t)(uintptr_t)info[i];
    }
    ret = next_ioctl(fd, request, &translated);
    if (ret == 0) {
        for (i = 0; i < 2; ++i) {
            void *destination = (void *)(uintptr_t)legacy->info[i];
            if (destination)
                info_to_legacy(destination, info[i]);
        }
    }
    return ret;
}

static int handle_getinfo2(int fd, int request,
                           struct compat_getinfo2 *legacy)
{
    struct compat_getinfo2 translated = *legacy;
    uint8_t info[NEW_INFO2_SIZE] = {0};
    int ret;

    translated.info = (uint32_t)(uintptr_t)info;
    ret = next_ioctl(fd, request, &translated);
    if (ret == 0 && legacy->info)
        info2_to_legacy((void *)(uintptr_t)legacy->info, info);
    return ret;
}

static int handle_feature(int fd, int request, struct compat_feature *feature)
{
    uint32_t legacy_id = feature->feature_id;
    int ret;

    if (legacy_id == 3112) { /* legacy SENSOR_FEATURE_GET_MUTE_STATE */
        if (feature->parameter)
            *(uint32_t *)(uintptr_t)feature->parameter = 0;
        return 0;
    }

    feature->feature_id = feature_to_kernel(legacy_id);
    ret = next_ioctl(fd, request, feature);
    feature->feature_id = legacy_id;
    return ret;
}

static int handle_control(int fd, int request, struct compat_control *control)
{
    uint32_t legacy_scenario = control->scenario;
    int ret;

    control->scenario = scenario_to_kernel(legacy_scenario);
    ret = next_ioctl(fd, request, control);
    control->scenario = legacy_scenario;
    return ret;
}

static int handle_mclk(int fd, struct old_mclk *legacy)
{
    struct new_mclk translated = {
        .on = legacy->on,
        .frequency_group = legacy->frequency_group,
        .timing_generator = 0,
    };
    int request = _IOWR('i', 60, struct new_mclk);

    return next_ioctl(fd, request, &translated);
}

int ioctl(int fd, int request, ...)
{
    va_list arguments;
    void *argument;
    unsigned int type = _IOC_TYPE(request);
    unsigned int number = _IOC_NR(request);
    unsigned int size = _IOC_SIZE(request);

    if (!next_ioctl)
        next_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");

    /* These imgsensor _IO commands have no third argument. Reading one from
     * the variadic list is undefined even though the kernel ignores it. */
    if (type == 'i' && size == 0 &&
        (number == 0 || number == 25 || number == 30 || number == 50))
        return next_ioctl(fd, request);

    va_start(arguments, request);
    argument = va_arg(arguments, void *);
    va_end(arguments);

    if (type != 'i' || !argument)
        return next_ioctl(fd, request, argument);

    if (number == 5 && size == sizeof(struct compat_getinfo))
        return handle_getinfo(fd, request, argument);
    if (number == 65 && size == sizeof(struct compat_getinfo2))
        return handle_getinfo2(fd, request, argument);
    if (number == 15 && size == sizeof(struct compat_feature))
        return handle_feature(fd, request, argument);
    if (number == 20 && size == sizeof(struct compat_control))
        return handle_control(fd, request, argument);
    if (number == 60 && size == sizeof(struct old_mclk))
        return handle_mclk(fd, argument);

    return next_ioctl(fd, request, argument);
}
