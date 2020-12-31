#ifndef DUCKYDD_VARS_H
#define DUCKYDD_VARS_H

#include <stdbool.h>

// general macros
#define MAX_SIZE_EVENTS 40
#define MAX_LOGLEVEL 2

// external buffers
#define MAX_SIZE_PATH 200

// internal buffers
#define MAX_SIZE_FUNCTION_NAME 50
#define MAX_SIZE_FORMAT_STRING 100

// logkeys
#define MAX_SIZE_SCANCODE 255
#define MAX_SIZE_KBSTRING 512

// global variables
extern bool g_brexit;
extern bool g_reloadconfig;
extern bool g_daemonize;
extern short g_loglevel;

#endif
