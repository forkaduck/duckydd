
#ifndef DUCKYDD_MAIN_H
#define DUCKYDD_MAIN_H

#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include <xkbcommon/xkbcommon.h>

#include "io.h"
#include "mbuffer.h"

#define MAX_SIZE_EVENTS 40

bool brexit;
bool reloadconfig;
bool daemonize;
short loglvl;

struct udevInfo {
        int udevfd;
        struct udev *udev;
        struct udev_device *dev;
        struct udev_monitor *mon;
};

struct deviceInfo {
        char openfd[MAX_SIZE_PATH];
        int fd;

        struct managedBuffer devlog;
        struct timespec time_added;

		union {
        	struct xkb_state *xstate;
			
	    	//kstate in bits: Capslock / CtrlR / CtrlL / ShiftR / ShiftL / Alt / Control / AltGr / Shift
			//from keyboard.h
			uint16_t kstate;
		};
		
        int score;
};
#endif
