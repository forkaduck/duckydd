#ifndef UDEV_DUCKYDD_H
#define UDEV_DUCKYDD_H

#include "main.h"

int has_tty ( struct udevInfo udev );
int init_udev ( struct udevInfo *udev );
void deinit_udev ( struct udevInfo *udev );

#endif
