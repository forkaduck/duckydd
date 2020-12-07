#ifndef DUCKYDD_IO_H
#define DUCKYDD_IO_H

#include <inttypes.h>
#include <stdbool.h>
#include <time.h>

#include "mbuffer.h"
#include "vars.h"

#define LOG(loglvl, format, args...) _logger(loglvl, __func__, format, ##args) // print function name
//#define LOG(loglvl, format, args...) _logger(loglvl, NULL, format, ##args) // disable

#define ERR(function) _logger(-1, __func__, "%s has failed (%d) -> %s (err: %d)\n", function, errno, strerror(errno))

// holds data read from the config file (mainly used by readconfig)
struct configInfo {
    long int maxcount;

    char logpath[MAX_SIZE_PATH];

    // kbd
    bool xkeymaps;

    int configfd;
    struct timespec minavrg;
};

// holds data that was parsed out by handleargs
struct argInfo {
    char configpath[MAX_SIZE_PATH];
};

int readconfig(const char path[], struct configInfo* data);
int handleargs(int argc, char* argv[], struct argInfo* data);

// internal logger function
char* binexpand(uint8_t bin, size_t size);
void _logger(short loglevel, const char func[], const char format[], ...);

// better strcmp implementations
int strcmp_ss(const char str1[], const char str2[]);
int strncmp_ss(const char str1[], const char str2[], size_t length);

const char* find_file(const char* input);
#endif
