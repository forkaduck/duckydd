
#ifndef DUCKYDD_MAIN_H
#define DUCKYDD_MAIN_H

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include <xkbcommon/xkbcommon.h>

#include "io.h"
#include "mbuffer.h"
#include "vars.h"

struct udevInfo {
    int udevfd;
    struct udev* udev;
    struct udev_device* dev;
    struct udev_monitor* mon;
};

struct deviceInfo {
    char openfd[PATH_MAX];
    int fd;

    struct managedBuffer devlog; // holds keystrokes pressed

    union {
        struct xkb_state* xstate; // xkbcommon state

        //kstate in bits: Capslock / CtrlR / CtrlL / ShiftR / ShiftL / Alt / Control / AltGr / Shift
        //from keyboard.h
        uint16_t kstate;
    };

    int score;
    bool locked;

    struct timespec lasttime;
    struct managedBuffer strokesdiff; // holds time differences between keystrokes in queue
    size_t currdiff;
};
#endif
