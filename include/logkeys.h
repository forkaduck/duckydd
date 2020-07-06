#ifndef LOGKEYS_DUCKYDD_H
#define LOGKEYS_DUCKYDD_H

#include <linux/input.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <xkbcommon/xkbcommon.h>
#include <xcb/xcb.h>

#include "io.h"
#include "main.h"

#define MAX_SIZE_SCANCODE 255
#define MAX_SIZE_KBSTRING 512

struct keyboardInfo {
        int outfd;
		
		union {
				struct {
						struct xkb_context *ctx;
						struct xkb_keymap *keymap;
						xcb_connection_t *con;
						int32_t device_id;
				} x;
			
				struct {
						unsigned int keycode[MAX_SIZE_SCANCODE];
						unsigned short actioncode[MAX_NR_KEYMAPS][NR_KEYS];
						unsigned char string[MAX_NR_FUNC][MAX_SIZE_KBSTRING];
				} k;
			};
        
};

int init_keylogging ( const char input[], struct keyboardInfo *kbd, struct configInfo *config );
int deinit_keylogging ( struct keyboardInfo *kbd, struct configInfo *config );

int logkey ( struct keyboardInfo *kbd, struct deviceInfo* device, struct input_event event, struct configInfo *config );

#endif
