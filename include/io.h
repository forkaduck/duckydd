#ifndef STUFF_IO_H
#define STUFF_IO_H

bool daemonize;

#define LOG(fd, format, args...) _logger(fd, __func__, format, ##args) // print function name
//#define LOG(fd, format, args...) _logger(fd, NULL, format, ##args) // disable

// external buffers
#define MAX_BUFFER_SIZE 400
#define MAX_PATH_SIZE 200

// internal buffers
#define MAX_FUNCTION_NAME_SIZE 50
#define MAX_FORMAT_STRING_NAME 100

// holds data read from the config file (mainly used by readconfig)
struct config_data {
    struct timespec maxtime;
    long int maxcount;

    size_t *blacklist;
    size_t blacklistsize;

    int configfd;
};

// holds data that was parsed out by handleargs
struct arg_data {
    char configpath[MAX_PATH_SIZE];
    char logpath[MAX_PATH_SIZE];
};

int readconfig(const char path[], struct config_data *data);
void handleargs(int argc, char *argv[], struct arg_data *data);

// internal logger function
void _logger(FILE *fd, const char func[], const char format[], ...);

// better strcmp implementations
int strcmp_ss(const char str1[], const char str2[]);
int strncmp_ss(const char str1[], const char str2[], size_t length);
#endif
