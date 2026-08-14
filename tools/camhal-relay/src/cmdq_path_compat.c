#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define LEGACY_CMDQ_PATH "/proc/mtk_cmdq"
#define CURRENT_CMDQ_PATH "/dev/mtk_cmdq"

static const char *translate_path(const char *path) {
    if (path && strcmp(path, LEGACY_CMDQ_PATH) == 0) {
        static unsigned int redirects;
        unsigned int redirect =
            __atomic_add_fetch(&redirects, 1, __ATOMIC_RELAXED);
        if (redirect <= 3) {
            __android_log_print(ANDROID_LOG_INFO, "Mt8163CmdqCompat",
                                "redirect %s to %s", LEGACY_CMDQ_PATH,
                                CURRENT_CMDQ_PATH);
        }
        return CURRENT_CMDQ_PATH;
    }
    return path;
}

static mode_t read_mode_if_needed(int flags, va_list arguments) {
    if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE) {
        return (mode_t)va_arg(arguments, int);
    }
    return 0;
}

int open(const char *path, int flags, ...) {
    va_list arguments;
    va_start(arguments, flags);
    mode_t mode = read_mode_if_needed(flags, arguments);
    va_end(arguments);
    return (int)syscall(__NR_openat, AT_FDCWD, translate_path(path), flags,
                        mode);
}

int open64(const char *path, int flags, ...) {
    va_list arguments;
    va_start(arguments, flags);
    mode_t mode = read_mode_if_needed(flags, arguments);
    va_end(arguments);
    return (int)syscall(__NR_openat, AT_FDCWD, translate_path(path), flags,
                        mode);
}

int openat(int directory_fd, const char *path, int flags, ...) {
    va_list arguments;
    va_start(arguments, flags);
    mode_t mode = read_mode_if_needed(flags, arguments);
    va_end(arguments);
    return (int)syscall(__NR_openat, directory_fd, translate_path(path), flags,
                        mode);
}

int openat64(int directory_fd, const char *path, int flags, ...) {
    va_list arguments;
    va_start(arguments, flags);
    mode_t mode = read_mode_if_needed(flags, arguments);
    va_end(arguments);
    return (int)syscall(__NR_openat, directory_fd, translate_path(path), flags,
                        mode);
}
