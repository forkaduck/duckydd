
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#include "io.h"
#include "logkeys.h"
#include "main.h"
#include "mbuffer.h"

#define PREFIX char
#define T char
#include "mbuffertemplate.h"

#define PREFIX struct_timespec
#define T struct timespec
#include "mbuffertemplate.h"

enum {
    KEY_STATE_RELEASE = 0,
    KEY_STATE_PRESS = 1,
    KEY_STATE_REPEAT = 2,
};

static const size_t conpath_size = 6;
static const char* conpath[] = {
    "/proc/self/fd/0",
    "/dev/tty",
    "/dev/tty0",
    "/dev/vc/0",
    "/dev/systty",
    "/dev/console",
    NULL
};

static int open_console0()
{
    size_t i;

    // go through some console paths
    for (i = 0; i < conpath_size; i++) {
        int fd;

        fd = open(conpath[i], O_NOCTTY | O_RDONLY);
        if (fd >= 0) {
            char ioctlarg;

            // if it is a tty and has the right keyboard return the fd
            if (!ioctl(fd, KDGKBTYPE, &ioctlarg)) {
                if (isatty(fd) && ioctlarg == KB_101) {
                    return fd;
                }
            }

            if (close(fd)) {
                ERR("close");
            }
        }

        LOG(1, "Failed to open %s! Trying next one ...\n", conpath[i]);
    }

    return -1;
}

static int load_x_keymaps(const char screen[], struct keyboardInfo* kbd, struct configInfo* config)
{
    int err = 0;

    // initalize x keymap
    kbd->x.keymap = NULL;

    // create a new context
    kbd->x.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!kbd->x.ctx) {
        LOG(-1, "xkb_context_new failed!\n");
        err = -1;
        goto error_exit;
    }

    // connect to the x server
    kbd->x.con = xcb_connect(screen, NULL);
    if (kbd->x.con == NULL) {
        LOG(-1, "xcb_connect failed!\n");
        err = -2;
        goto error_exit;
    }

    if (xcb_connection_has_error(kbd->x.con)) {
        LOG(-1, "xcb_connection_has_error failed!\n");
        err = -3;
        goto error_exit;
    }

    // setup the xkb extension
    {
        uint16_t major_xkb, minor_xkb;
        uint8_t event_out, error_out;
        if (!xkb_x11_setup_xkb_extension(kbd->x.con, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION, 0, &major_xkb, &minor_xkb, &event_out, &error_out)) {
            LOG(-1, "xkb_x11_setup_xkb_extension failed with %d! -> major_xkb %d minor_xkb %d\n", error_out, major_xkb, minor_xkb);
            err = -4;
            goto error_exit;
        }
    }

    // get device id of the core x keyboard
    kbd->x.device_id = xkb_x11_get_core_keyboard_device_id(kbd->x.con);
    if (kbd->x.device_id == -1) {
        LOG(-1, "xkb_x11_get_core_keyboard_device_id failed!\n");
        err = -5;
        goto error_exit;
    }

    // get keymap
    kbd->x.keymap = xkb_x11_keymap_new_from_device(kbd->x.ctx, kbd->x.con, kbd->x.device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!kbd->x.keymap) {
        LOG(-1, "xkb_x11_keymap_new_from_device failed!\n");
        err = -6;
        goto error_exit;
    }

    return err;

error_exit:
    config->xkeymaps = false;

    // get rid of unused heap space
    if (kbd->x.keymap) {
        xkb_keymap_unref(kbd->x.keymap);
    }

    if (kbd->x.ctx) {
        xkb_context_unref(kbd->x.ctx);
    }
    return err;
}

static int load_kernel_keymaps(const int fd, struct keyboardInfo* kbd, struct configInfo* config)
{
    size_t i, k;

    // load scancode to keycode table
    for (i = 0; i < MAX_SIZE_SCANCODE; i++) {
        struct kbkeycode temp;

        temp.scancode = i;
        temp.keycode = 0;

        if (ioctl(fd, KDGETKEYCODE, &temp)) {
            ERR("ioctl");
            return -1;
        }

        kbd->k.keycode[i] = temp.keycode;
    }

    // load keycode to actioncode table
    for (i = 0; i < MAX_NR_KEYMAPS; i++) {
        for (k = 0; k < NR_KEYS; k++) {
            struct kbentry temp;

            temp.kb_table = i;
            temp.kb_index = k;
            temp.kb_value = 0;

            if (ioctl(fd, KDGKBENT, &temp)) {
                ERR("ioctl");
                return -2;
            }

            kbd->k.actioncode[i][k] = temp.kb_value;
        }
    }

    // loads actioncode to string table
    for (i = 0; i < MAX_NR_FUNC; i++) {
        struct kbsentry temp;

        temp.kb_func = i;
        temp.kb_string[0] = '\0';

        if (ioctl(fd, KDGKBSENT, &temp)) {
            ERR("ioctl");
            return -3;
        }

        memcpy_s(kbd->k.string[i], MAX_SIZE_KBSTRING, temp.kb_string, MAX_SIZE_KBSTRING);
    }

    return 0;
}

int init_keylogging(const char input[], struct keyboardInfo* kbd, struct configInfo* config)
{
    int err = 0;

    // setup x keymaps
    if (config->xkeymaps) {
        if (load_x_keymaps(input, kbd, config)) {
            LOG(-1, "init_xkeylogging failed!\n");
        }
    }

    // use kernel keymaps
    if (!config->xkeymaps) {
        int fd;

        // get a file descriptor to console 0
        fd = open_console0();
        if (fd < 0) {
            LOG(-1, "Failed to open a file descriptor to a vaild console!\n");
            err = -1;
            goto error_exit;
        }

        // load keytable / accent table / and scancode translation table
        if (load_kernel_keymaps(fd, kbd, config)) {
            LOG(-1, "init_kernelkeylogging failed!\n");
            err = -2;
            goto error_exit;
        }
    }

    // create keylog file
    {
        const char file[] = { "/key.log" };
        char path[sizeof(config->logpath) + sizeof(file)];

        strcpy_s(path, sizeof(path), config->logpath);
        strcat_s(path, sizeof(path), file);

        kbd->outfd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_NOCTTY, S_IRUSR | S_IWUSR);
        if (kbd->outfd < 0) {
            ERR("open");
            err = -3;
            goto error_exit;
        }
    }

    LOG(1, "Keylogging ready!\n");
    return err;

error_exit:
    LOG(-1, "Turning of keylogging because the init of both the kernel keymaps and libxkbcommon failed!\n");
    return err;
}

int deinit_keylogging(struct keyboardInfo* kbd, struct configInfo* config)
{
    if (close(kbd->outfd)) {
        ERR("close");
    }

    if (config->xkeymaps) {
        xkb_keymap_unref(kbd->x.keymap);
        xkb_context_unref(kbd->x.ctx);
        xcb_disconnect(kbd->x.con);
    }
    return 0;
}

// translate scancode to string
static int interpret_keycode(struct managedBuffer* buff, struct deviceInfo* device, struct keyboardInfo* kbd, unsigned int code, uint8_t value)
{
    uint16_t modmask = 0;

    LOG(1, "keycode:%d value:%d - typ:%d val:%d\n", code, value, KTYP(code), KVAL(code));

    // Capslock / CtrlR / CtrlL / ShiftR / ShiftL / Alt / Control / AltGr / Shift
    switch (code) { //  (1 <= scancode <= 88) -> keycode == scancode
    case KEY_RIGHTSHIFT:
    case KEY_LEFTSHIFT:
        if (code == KEY_RIGHTSHIFT) {
            modmask |= 1 << 5;
        } else {
            modmask |= 1 << 4;
        }
        modmask |= 1;
        break;

    case KEY_RIGHTCTRL:
    case KEY_LEFTCTRL:
        if (code == KEY_RIGHTCTRL) {
            modmask |= 1 << 7;
        } else {
            modmask |= 1 << 6;
        }
        modmask |= 1 << 2;
        break;

    case KEY_RIGHTALT:
    case KEY_LEFTALT:
        modmask |= 1 << 3;

        /*case ??: //TODO
                modmask |= 1 << 1;*/

    case KEY_CAPSLOCK:
        modmask |= 1 << 8;
    }

    if (value) { // change modifier state
        device->kstate |= modmask;
    } else {
        device->kstate &= (0xffff ^ modmask);
    }

    if (!modmask) { // not a mod key
        if (value == KEY_STATE_PRESS) {
            if (code < 0x1000) { // is not unicode
                unsigned short actioncode;

                if (KTYP(code) != KT_META) {
                    actioncode = kbd->k.actioncode[device->kstate][kbd->k.keycode[code]];
                } else {
                    return 0;
                }

                LOG(1, "actioncode:%d -> %c\n", actioncode, KVAL(actioncode));

                if (m_append_member_char(buff, KVAL(actioncode))) {
                    LOG(0, "m_append_member_char failed!\n");
                }

            } else { // unicode  // TODO UNTESTED
                unsigned short actioncode;
                size_t i;
                unsigned short limit;

                code ^= 0xf000;
                actioncode = kbd->k.actioncode[device->kstate][kbd->k.keycode[code]];
                limit = 0x0;

                for (i = 0; i < 4; i++) { // write unicode character utf-8 encoded into the logbuffer
                    limit |= 0xff << i;
                    if (code > limit) {
                        if (m_append_member_char(buff, actioncode & (0xff << (7 * i)))) {
                            LOG(0, "m_append_member_char failed!\n");
                        }
                    }
                }
            }
        }

    } else {
        char* bin = binexpand(device->kstate, 8);

        LOG(1, "kstate:%s\n", bin);
        free(bin);
    }
    return 0;
}

static int check_if_evil(struct deviceInfo* device, struct configInfo* config)
{
    // increment currdiff until wraparound
    if (device->currdiff < device->strokesdiff.size) {
        struct timespec temp;

        // get current time
        if (clock_gettime(CLOCK_REALTIME, &temp)) {
            ERR("clock_gettime");
            return -1;
        }

        // caluclate time difference
        m_struct_timespec(&device->strokesdiff)[device->currdiff].tv_sec = temp.tv_sec - device->lasttime.tv_sec;
        m_struct_timespec(&device->strokesdiff)[device->currdiff].tv_nsec = temp.tv_nsec - device->lasttime.tv_nsec;
        
        // save last value
        device->lasttime.tv_sec = temp.tv_sec;
        device->lasttime.tv_nsec = temp.tv_nsec;

        // if the queue is filled then use it to calculate the average difference
        if (m_struct_timespec(&device->strokesdiff)[device->strokesdiff.size - 1].tv_sec != 0 || m_struct_timespec(&device->strokesdiff)[device->strokesdiff.size - 1].tv_nsec != 0) {
            size_t i;
            struct timespec sum;

            sum.tv_sec = 0;
            sum.tv_nsec = 0;

            // calculate average of the array
            for (i = 0; i < device->strokesdiff.size; i++) {
                struct timespec curr;

                curr = m_struct_timespec(&device->strokesdiff)[i];

                sum.tv_sec += curr.tv_sec;
                sum.tv_nsec += curr.tv_nsec;
            }

            sum.tv_sec /= device->strokesdiff.size;
            sum.tv_nsec /= device->strokesdiff.size;

            LOG(2, "Average time: %ds %dns\n", sum.tv_sec, sum.tv_nsec);

            if (sum.tv_sec < config->minavrg.tv_sec || (sum.tv_sec == config->minavrg.tv_sec && sum.tv_nsec < config->minavrg.tv_nsec)) {
                device->score++;
            }
        }
        device->currdiff++;
    } else {
        device->currdiff = 0;
    }

    return 0;
}

int logkey(struct keyboardInfo* kbd, struct deviceInfo* device, struct input_event event, struct configInfo* config)
{
    // if xkbcommon lib has initalized
    if (config->xkeymaps) {
        xkb_keycode_t keycode = event.code + 8;

        // if key repeates
        if (event.value == KEY_STATE_REPEAT && !xkb_keymap_key_repeats(kbd->x.keymap, event.code)) {
            return 0;
        }

        // track key releases
        if (event.value == KEY_STATE_RELEASE) {
            xkb_state_update_key(device->xstate, keycode, XKB_KEY_UP);
        } else {
            size_t size;

            xkb_state_update_key(device->xstate, keycode, XKB_KEY_DOWN);

            // get the size of the required buffer
            size = xkb_state_key_get_utf8(device->xstate, keycode, NULL, 0) + 1;
            if (size > 1) {
                char* buffer;

                buffer = malloc(size);
                if (buffer == NULL) {
                    ERR("malloc");
                    return -1;
                }

                // get the buffers
                xkb_state_key_get_utf8(device->xstate, keycode, buffer, size);

                if (m_append_array_char(&device->devlog, buffer, size - 1)) { // dont copy \0
                    LOG(-1, "append_mbuffer_array_char failed!\n");
                    return -2;
                }
                free(buffer);

            } else if (size != 1) {
                LOG(-1, "Keyevent without valid key in map (%d)!\n", keycode);
            }
        }

    } else {
        // interpret the keycode using the kernel keytable
        if (interpret_keycode(&device->devlog, device, kbd, event.code, event.value)) {
            LOG(-1, "codetoksym failed!\n");
            return -3;
        }
        printf("\n");
    }

    // check if the keystrokes are typed in a suspicious way
    if (check_if_evil(device, config)) {
        LOG(-1, "check_if_evil failed\n");
        return -4;
    }

    return 0;
}
