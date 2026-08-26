#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

static void print_banner(void)
{
    puts("=========================================================================");
    puts("");
    puts("                   **************************************");
    puts("                    #                                  =");
    puts("                     +                                *");
    puts("                      =.             :==.            *");
    puts("                       ::         =@@@@@@@@.        *");
    puts("                        .-       %@@@@@@@@@@:      *");
    puts("                          +      @@@@@@@@@@@#    .=");
    puts("                           #     @@@@@@@@@@@+   .-");
    puts("                            +    :@@@@@@@@@%   ::");
    puts("                             =.    =@@@@@%.   =");
    puts("                              =.             +");
    puts("                             * .-           *");
    puts("                           :-    =         *");
    puts("                       =  *       *       *");
    puts("                        ==         +     =");
    puts("                 +     =. *         *. .-");
    puts("                  ::  %     =        -.:");
    puts("                    *-       =       =.-");
    puts("                   +  *             *   =");
    puts("                  =    ::          *     +");
    puts("                         .        *       *");
    puts("                                 *         *");
    puts("                                =           =");
    puts("                              :-             .:");
    puts("                             .:               .=");
    puts("                            -                   =");
    puts("                           +                     *");
    puts("                   +      *                       *");
    puts("                   +     *                         =.");
    puts("                   +    *                           ::");
    puts("                   +   =                             .-");
    puts("                   +");
    puts("                   ++++++++=");
    puts("");
    puts("luna nulls ^_^  -  https://discord.gg/tXx4zYDm98");
    puts("=========================================================================");
    puts("");
}

#define MAX_KEYS 1024

static int uifd = -1;
static int phys[MAX_KEYS]      = {0};
static int virt[MAX_KEYS]      = {0};
static int prev_virt[MAX_KEYS] = {0};
static int last_ad = 0;
static int g_wrelease_enabled = 1;

#define WRELEASE_TOGGLE_KEY KEY_F9

static inline void io_write(int fd, const void *buf, size_t n) {
    if (write(fd, buf, n) < 0) { /* best-effort, virtual device */ }
}
static inline void io_drain(int fd) {
    char buf[4096];
    while (read(fd, buf, sizeof(buf)) > 0);
}

static void emit(int type, int code, int val) {
    struct input_event ev = {0};
    ev.type  = type;
    ev.code  = code;
    ev.value = val;
    io_write(uifd, &ev, sizeof(ev));
}

static int effective_virt(int code) {
    if (!phys[code]) return 0;
    if ((code == KEY_A || code == KEY_D) && phys[KEY_A] && phys[KEY_D])
        return (code == KEY_A) ? (last_ad == 1) : (last_ad == 2);
    if (g_wrelease_enabled && code == KEY_W && phys[KEY_W] && phys[KEY_SPACE])
        return 0;
    return phys[code];
}

static void recalc_socd(void) {
    for (int i = 0; i < MAX_KEYS; i++)
        virt[i] = effective_virt(i);
}

static void release_all_keys(void) {
    for (int i = 0; i < MAX_KEYS; i++) {
        if (prev_virt[i]) emit(EV_KEY, i, 0);
        phys[i] = virt[i] = prev_virt[i] = 0;
    }
    emit(EV_SYN, SYN_REPORT, 0);
    last_ad = 0;
}

static int find_keyboard_event_ex(char *out, size_t max, int want_keyd) {
    FILE *f = fopen("/proc/bus/input/devices", "r");
    if (!f) return -1;
    char line[256], handlers[256] = {0}, name[256] = {0};
    int is_kb = 0, found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "N: Name=", 8) == 0)
            strncpy(name, line + 8, sizeof(name) - 1);
        if (strstr(line, "EV=120013")) is_kb = 1;
        if (strncmp(line, "H: Handlers=", 12) == 0)
            strncpy(handlers, line + 12, sizeof(handlers) - 1);
        if (line[0] == '\n' || line[0] == '\r') {
            int is_keyd_dev  = strcasestr(name, "keyd") != NULL;
            int is_our_dev   = strcasestr(name, "bhop-kb-cleaner") != NULL;
            int name_matches = want_keyd ? is_keyd_dev : (is_kb && !is_keyd_dev && !is_our_dev);
            if (name_matches) {
                char *p = strstr(handlers, "event");
                int n = -1;
                if (p && sscanf(p, "event%d", &n) == 1) {
                    snprintf(out, max, "/dev/input/event%d", n);
                    found = 1; break;
                }
            }
            is_kb = 0; handlers[0] = '\0'; name[0] = '\0';
        }
    }
    fclose(f);
    return found ? 0 : -1;
}

static int find_keyboard_event(char *out, size_t max) {
    return find_keyboard_event_ex(out, max, 0);
}

static int find_keyd_event(char *out, size_t max) {
    return find_keyboard_event_ex(out, max, 1);
}

static int open_keyboard(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { perror("open input"); return -1; }

    int rep[2] = {0, 0};
    ioctl(fd, EVIOCSREP, rep);

    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        int saved_errno = errno;
        fprintf(stderr, "grab %s: %s\n", path, strerror(saved_errno));
        close(fd);
        errno = saved_errno;
        return -1;
    }
    printf("Opened: %s\n", path);
    return fd;
}

static int create_vkeyboard(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open uinput"); return -1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int i = 0; i < KEY_MAX; i++) ioctl(fd, UI_SET_KEYBIT, i);
    struct uinput_setup us = {0};
    us.id.bustype = BUS_USB;
    us.id.vendor  = 0x1234;
    us.id.product = 0x5678;
    strcpy(us.name, "bhop-kb-cleaner");
    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

int main(int argc, char *argv[]) {
    print_banner();

    char dev_path[128] = {0};
    int manual_path = 0;

    if (argc > 1) {
        strncpy(dev_path, argv[1], sizeof(dev_path) - 1);
        manual_path = 1;
    }

    uifd = create_vkeyboard();
    if (uifd < 0) return 1;

    int inofd = inotify_init1(IN_NONBLOCK);
    if (inofd < 0) { perror("inotify_init1"); return 1; }
    if (inotify_add_watch(inofd, "/dev/input", IN_CREATE | IN_DELETE) < 0) {
        perror("inotify_add_watch"); return 1;
    }

    int infd = -1;
    printf("Virtual keyboard active. Waiting for keyboard...\n");
    printf("Press F9 to toggle w-release (on by default)\n");

    struct pollfd fds[2];

    while (1) {
        if (infd < 0) {
            if (!manual_path && find_keyboard_event(dev_path, sizeof(dev_path)) < 0) {
                /* nothing found, sleep until something appears in /dev/input */
                fds[0].fd = inofd; fds[0].events = POLLIN;
                poll(fds, 1, 2000);
                io_drain(inofd);
                continue;
            }
            infd = open_keyboard(dev_path);
            if (infd < 0) {
                if (errno == EBUSY && !manual_path) {
                    char keyd_path[128];
                    if (find_keyd_event(keyd_path, sizeof(keyd_path)) == 0) {
                        fprintf(stderr, "Physical device busy (likely keyd). "
                                        "Falling back to keyd's virtual keyboard: %s\n", keyd_path);
                        infd = open_keyboard(keyd_path);
                        if (infd >= 0) strncpy(dev_path, keyd_path, sizeof(dev_path) - 1);
                    }
                }
                if (infd < 0) { sleep(1); continue; }
            }
            io_drain(inofd);
        }

        fds[0].fd = infd;  fds[0].events = POLLIN;
        fds[1].fd = inofd; fds[1].events = POLLIN;

        int ret = poll(fds, 2, -1);
        if (ret < 0) { if (errno == EINTR) continue; perror("poll"); break; }

        if (fds[1].revents & POLLIN)
            io_drain(inofd);

        if (fds[0].revents & (POLLHUP | POLLERR)) {
            printf("Keyboard hung up.\n");
            release_all_keys();
            ioctl(infd, EVIOCGRAB, 0); close(infd); infd = -1;
            if (!manual_path) dev_path[0] = '\0';
            continue;
        }

        if (!(fds[0].revents & POLLIN)) continue;

        struct input_event ev;
        while (1) {
            ssize_t r = read(infd, &ev, sizeof(ev));
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                printf("Read error: %s. Keyboard disconnected.\n", strerror(errno));
                release_all_keys();
                ioctl(infd, EVIOCGRAB, 0); close(infd); infd = -1;
                if (!manual_path) dev_path[0] = '\0';
                break;
            }
            if (r == 0) break;

            if (ev.type == EV_KEY && ev.code < MAX_KEYS) {
                if (ev.value == 2) continue;

                phys[ev.code] = ev.value;
                if (ev.code == KEY_A && ev.value == 1) last_ad = 1;
                if (ev.code == KEY_D && ev.value == 1) last_ad = 2;

                if (ev.code == WRELEASE_TOGGLE_KEY && ev.value == 1) {
                    g_wrelease_enabled = !g_wrelease_enabled;
                    printf("w-release %s\n", g_wrelease_enabled ? "ON" : "OFF");
                }

                recalc_socd();

            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                int changed = 0;
                for (int i = 0; i < MAX_KEYS; i++) {
                    if (virt[i] != prev_virt[i]) {
                        emit(EV_KEY, i, virt[i]);
                        prev_virt[i] = virt[i];
                        changed = 1;
                    }
                }
                if (changed) emit(EV_SYN, SYN_REPORT, 0);
            }
        }
    }

    if (infd >= 0) { ioctl(infd, EVIOCGRAB, 0); close(infd); }
    ioctl(uifd, UI_DEV_DESTROY); close(uifd);
    close(inofd);
    return 0;
}
