#ifndef STUFF_IO_H
#define STUFF_IO_H

#include <time.h>
#include <stdbool.h>

#include "mbuffer.h"

// external buffers
#define MAX_SIZE_PATH 200

// internal buffers
#define MAX_SIZE_FUNCTION_NAME 50
#define MAX_SIZE_FORMAT_STRING 100

//#define LOG(loglvl, format, args...) _logger(loglvl, __func__, format, ##args) // print function name
#define LOG(loglvl, format, args...) _logger(loglvl, NULL, format, ##args) // disable
#define ERR(function) _logger(-1, __func__, "%s has failed (%d) -> %s\n", function, errno, strerror(errno))

// holds data read from the config file (mainly used by readconfig)
struct configInfo {
        struct timespec maxtime;
        long int maxcount;

        char logpath[MAX_SIZE_PATH];

        // kbd
        struct managedBuffer blacklist;
        bool logkeys;

        int configfd;
};

// holds data that was parsed out by handleargs
struct argInfo {
        char configpath[MAX_SIZE_PATH];
};

int readconfig ( const char path[], struct configInfo *data );
int handleargs ( int argc, char *argv[], struct argInfo *data );

// internal logger function
void _logger ( short loglevel, const char func[], const char format[], ... );

// better strcmp implementations
int strcmp_ss ( const char str1[], const char str2[] );
int strncmp_ss ( const char str1[], const char str2[], size_t length );

const char *find_file ( const char *input );
#endif
