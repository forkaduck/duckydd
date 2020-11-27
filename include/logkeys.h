#ifndef LOGKEYS_DUCKYDD_H
#define LOGKEYS_DUCKYDD_H

#include <linux/input.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include "io.h"
#include "main.h"
#include "vars.h"

struct keyboardInfo {
    int outfd;

    union {
		// holds all xkbcommon variables
        struct {
            struct xkb_context* ctx;
            struct xkb_keymap* keymap;
            xcb_connection_t* con;
            int32_t device_id;
        } x;

		// holds all kernel maps
        struct {
            unsigned int keycode[MAX_SIZE_SCANCODE];
            unsigned short actioncode[MAX_NR_KEYMAPS][NR_KEYS];
            unsigned char string[MAX_NR_FUNC][MAX_SIZE_KBSTRING];
        } k;
    };
};

int init_keylogging(const char input[], struct keyboardInfo* kbd, struct configInfo* config);
int deinit_keylogging(struct keyboardInfo* kbd, struct configInfo* config);

int logkey(struct keyboardInfo* kbd, struct deviceInfo* device, struct input_event event, struct configInfo* config);

#endif
