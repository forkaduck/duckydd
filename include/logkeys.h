#ifndef LOGKEYS_DUCKYDD_H
#define LOGKEYS_DUCKYDD_H

#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>
#include <xcb/xcb.h>

#include "io.h"
#include "main.h"

struct keyboardInfo {
        int outfd;
        struct xkb_context *ctx;
        struct xkb_keymap *keymap;
        xcb_connection_t *con;
        int32_t device_id;
};

int init_keylogging ( const char input[], struct keyboardInfo *kbd, struct configInfo *config );
int deinit_keylogging ( struct keyboardInfo *kbd );

int logkey ( struct keyboardInfo *kbd, struct device* device, struct input_event event );

#endif
