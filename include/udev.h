#ifndef UDEV_DUCKYDD_H
#define UDEV_DUCKYDD_H

#include "main.h"

// checks if the device has a tty device associated with it
int has_tty(struct udevInfo udev);

// initalizer functions
int init_udev(struct udevInfo* udev);
void deinit_udev(struct udevInfo* udev);

#endif
