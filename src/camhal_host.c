#include "legacy_camera_hal.h"

#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_HAL_PATH "/data/local/tmp/camprobe/vendor/lib/hw/camera.mt8163.so"
#define MAX_PARAMETER_BYTES (1024u * 1024u)

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

static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s enumerate|open|parameters|close [camera-id] [hal-path]\n",
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
    if (result == 0 && strcmp(mode, "close") == 0) {
        result = close_camera(&state);
    }
    if (strcmp(mode, "enumerate") != 0 && strcmp(mode, "open") != 0 &&
        strcmp(mode, "parameters") != 0 && strcmp(mode, "close") != 0) {
        usage(argv[0]);
        result = 2;
    }

    fprintf(stderr, "[camhal] mode=%s result=%d; exiting without dlclose\n",
            mode, result);
    finish_without_unload(result);
}
