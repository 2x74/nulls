/* -- Anyone else like to make their notes look pretty?  -- */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <poll.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

/* -- Banner -- */

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
    puts("luna nulls ^_^  -  https://discord.gg/dnmJSFHmxN");
    puts("=========================================================================");
    puts("");
}

/* -- Globals -- */

static int ufd = -1;
static volatile int running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* -- Device setup -- */

/* Open all event devices whose name contains match */
static int open_matching(const char *match, int fds[], int max) {
    int count = 0;
    DIR *dir = opendir("/dev/input");
    if (!dir) return 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) && count < max) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[256] = {0};
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        if (strstr(name, "nulls-virt") || !strstr(name, match)) {
            close(fd);
            continue;
        }
        printf("  grabbing: %s (%s)\n", path, name);
        fds[count++] = fd;
    }
    closedir(dir);
    return count;
}

static void setup_uinput(void) {
    ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) { perror("open /dev/uinput"); exit(1); }

    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_REL);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);
    ioctl(ufd, UI_SET_EVBIT, EV_MSC);
    ioctl(ufd, UI_SET_MSCBIT, MSC_SCAN);

    for (int i = 0; i < KEY_MAX; i++) ioctl(ufd, UI_SET_KEYBIT, i);
    for (int i = 0; i < REL_MAX; i++) ioctl(ufd, UI_SET_RELBIT, i);

    struct uinput_setup us = {0};
    us.id.bustype = BUS_USB;
    us.id.vendor  = 0x1234;
    us.id.product = 0x5678;
    strncpy(us.name, "nulls-virt-device", UINPUT_MAX_NAME_SIZE);

    ioctl(ufd, UI_DEV_SETUP, &us);
    ioctl(ufd, UI_DEV_CREATE);
    usleep(200000);
}

static void emit(int type, int code, int val) {
    struct input_event ev = {0};
    ev.type = type; ev.code = code; ev.value = val;
    write(ufd, &ev, sizeof ev);
}

static void passthrough(struct input_event *ev) {
    emit(ev->type, ev->code, ev->value);
    if (ev->type == EV_KEY || ev->type == EV_REL)
        emit(EV_SYN, SYN_REPORT, 0);
}

/* -- The actual logic -- */

static int w_release_enabled = 1;

static void handle_event(struct input_event *ev,
                         int *a_held,  int *d_held,
                         int *w_held,  int *s_held,
                         int *a_active, int *d_active,
                         int *w_active, int *s_active)
{
    if (ev->type != EV_KEY) {
        passthrough(ev);
        return;
    }

    int code = ev->code;
    int val  = ev->value; /* 0 = up, 1 = down, 2 = repeat */

    /* --- EXIT key --- */
    if (code == KEY_F7 && val == 1) {
        running = 0;
        return;
    }

    /* --- Toggle W-release --- */
    if (code == KEY_INSERT && val == 1) {
        w_release_enabled = !w_release_enabled;
        printf("w release: %s\n", w_release_enabled ? "on" : "off");
        fflush(stdout);
        return;
    }

    /* --- Movement + space --- */
    int is_movement = (code == KEY_W || code == KEY_A ||
                       code == KEY_S || code == KEY_D ||
                       code == KEY_SPACE);
    if (is_movement && val == 2) return;

    /* --- WASD + space handling --- */
    if (code == KEY_A) {
        *a_held = val;
        if (val == 1) {
            if (*d_active) { emit(EV_KEY, KEY_D, 0); *d_active = 0; emit(EV_SYN, SYN_REPORT, 0); }
            emit(EV_KEY, KEY_A, 1); *a_active = 1;
        } else {
            emit(EV_KEY, KEY_A, 0); *a_active = 0;
            if (*d_held) { emit(EV_KEY, KEY_D, 1); *d_active = 1; }
        }
        emit(EV_SYN, SYN_REPORT, 0);
    }
    else if (code == KEY_D) {
        *d_held = val;
        if (val == 1) {
            if (*a_active) { emit(EV_KEY, KEY_A, 0); *a_active = 0; emit(EV_SYN, SYN_REPORT, 0); }
            emit(EV_KEY, KEY_D, 1); *d_active = 1;
        } else {
            emit(EV_KEY, KEY_D, 0); *d_active = 0;
            if (*a_held) { emit(EV_KEY, KEY_A, 1); *a_active = 1; }
        }
        emit(EV_SYN, SYN_REPORT, 0);
    }
    else if (code == KEY_W) {
        *w_held = val;
        if (val == 1) {
            if (*s_active) { emit(EV_KEY, KEY_S, 0); *s_active = 0; emit(EV_SYN, SYN_REPORT, 0); }
            emit(EV_KEY, KEY_W, 1); *w_active = 1;
        } else {
            emit(EV_KEY, KEY_W, 0); *w_active = 0;
            if (*s_held) { emit(EV_KEY, KEY_S, 1); *s_active = 1; }
        }
        emit(EV_SYN, SYN_REPORT, 0);
    }
    else if (code == KEY_S) {
        *s_held = val;
        if (val == 1) {
            if (*w_active) { emit(EV_KEY, KEY_W, 0); *w_active = 0; emit(EV_SYN, SYN_REPORT, 0); }
            emit(EV_KEY, KEY_S, 1); *s_active = 1;
        } else {
            emit(EV_KEY, KEY_S, 0); *s_active = 0;
            if (*w_held) { emit(EV_KEY, KEY_W, 1); *w_active = 1; }
        }
        emit(EV_SYN, SYN_REPORT, 0);
    }
    else if (code == KEY_SPACE) {
        /* W-release */
        if (val == 1 && w_release_enabled && *w_active) {
            emit(EV_KEY, KEY_W, 0); *w_active = 0;
            emit(EV_SYN, SYN_REPORT, 0);
        }
        emit(EV_KEY, KEY_SPACE, val);
        emit(EV_SYN, SYN_REPORT, 0);
    }
    else {
        passthrough(ev);
    }
}

int main(void) {
    print_banner();
    puts("INSERT: toggle w-release (default: on)");
    puts("F7:     exit\n");

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Pick your keyboard (name contains): ");
    fflush(stdout);
    char match[256] = {0};
    if (!fgets(match, sizeof match, stdin)) return 1;
    match[strcspn(match, "\n")] = 0;

    printf("\nAuto-grabbing all interfaces for: %s\n", match);

    int kfds[16];
    int ndevs = open_matching(match, kfds, 16);
    if (ndevs == 0) {
        fprintf(stderr, "No matching devices found.\n");
        return 1;
    }

    setup_uinput();

    struct pollfd pfds[16];
    for (int i = 0; i < ndevs; i++) {
        if (ioctl(kfds[i], EVIOCGRAB, (void*)1) < 0) {
            perror("EVIOCGRAB failed - try sudo");
            return 1;
        }
        pfds[i].fd     = kfds[i];
        pfds[i].events = POLLIN;
    }

    int a_held = 0, d_held = 0, w_held = 0, s_held = 0;
    int a_active = 0, d_active = 0, w_active = 0, s_active = 0;

    printf("\nReady.\n");

    struct input_event ev;
    while (running) {
        int ret = poll(pfds, ndevs, 100);
        if (ret < 0) { perror("poll"); break; }
        if (ret == 0) continue;
        for (int i = 0; i < ndevs; i++) {
            if (!(pfds[i].revents & POLLIN)) continue;
            while (read(kfds[i], &ev, sizeof ev) > 0) {
                handle_event(&ev,
                             &a_held,  &d_held,
                             &w_held,  &s_held,
                             &a_active, &d_active,
                             &w_active, &s_active);
            }
        }
    }

    /* Release any keys still active */
    if (w_active) { emit(EV_KEY, KEY_W, 0); emit(EV_SYN, SYN_REPORT, 0); }
    if (s_active) { emit(EV_KEY, KEY_S, 0); emit(EV_SYN, SYN_REPORT, 0); }
    if (a_active) { emit(EV_KEY, KEY_A, 0); emit(EV_SYN, SYN_REPORT, 0); }
    if (d_active) { emit(EV_KEY, KEY_D, 0); emit(EV_SYN, SYN_REPORT, 0); }

    for (int i = 0; i < ndevs; i++) {
        ioctl(kfds[i], EVIOCGRAB, (void*)0);
        close(kfds[i]);
    }
    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);

    puts("bye ^_^");
    return 0;
}

/* If you're reading this, skidding is bad!
 * Made by Luna. https://x.com/pishogue
 */
