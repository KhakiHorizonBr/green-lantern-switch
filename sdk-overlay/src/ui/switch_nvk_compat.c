/* Minimal Horizon libc adapters for the pinned experimental NVK build.
 * These are independent implementations. Keep the disk cache disabled until
 * its POSIX file-locking assumptions have a real Horizon implementation.
 */
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>

uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }

long sysconf(int name) {
    if (name == _SC_PAGESIZE) return 4096;
    if (name == _SC_PHYS_PAGES) return (3L * 1024 * 1024 * 1024) / 4096;
    if (name == _SC_NPROCESSORS_ONLN || name == _SC_NPROCESSORS_CONF) {
        u64 mask = 0;
        if (R_SUCCEEDED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
            return __builtin_popcountll(mask);
        return 3;
    }
    errno = EINVAL;
    return -1;
}

int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
    (void)how; (void)set;
    if (oldset) memset(oldset, 0, sizeof(*oldset));
    return 0;  /* Horizon does not deliver POSIX signals. */
}

int flock(int fd, int operation) {
    (void)fd; (void)operation;
    errno = ENOTSUP;
    return -1;
}
int dirfd(DIR* dir) {
    (void)dir;
    errno = ENOTSUP;
    return -1;
}
int fstatat(int fd, const char* path, struct stat* st, int flags) {
    (void)fd; (void)path; (void)st; (void)flags;
    errno = ENOTSUP;
    return -1;
}
int regcomp(regex_t* re, const char* pattern, int flags) {
    (void)re; (void)pattern; (void)flags;
    return REG_BADPAT;
}
int regexec(const regex_t* re, const char* str, size_t n, regmatch_t matches[], int flags) {
    (void)re; (void)str; (void)n; (void)matches; (void)flags;
    return REG_NOMATCH;
}
void regfree(regex_t* re) { (void)re; }
