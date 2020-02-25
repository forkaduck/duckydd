#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#include <safe_lib.h>
#include "io.h"

// config interpretation functions
static long long int parse_longlong(const char input[], char **end)
{
    long long int num;
    errno = 0;

    num = strtoll(input, end, 0);
    if(end != NULL) {
        if(*end == input) {
            return -1;
        }
    }

    if((num == LLONG_MAX || num == LLONG_MIN) && errno == ERANGE) {
        return -2;
    }
    return num;
}

static size_t readfile(int fd, char **output)
{
    char buffer[10];
    size_t readsize = -1;
    size_t size = 0;

    while(readsize != 0) { // read 10 bytes, reallocate the array and then copy them into the array
        char *temp;
        readsize = read(fd, buffer, 10);
        size += readsize;

        temp = realloc(*output, sizeof(char) * size);
        if(temp == NULL) {
            free(*output);
            *output = NULL;
            return 0;
        }
        *output = temp;

        memcpy_s(&(*output)[size - readsize], readsize, buffer, readsize);
    }
    return size;
}

int readconfig(const char path[], struct config_data *data)
{
    int err = 0;
    size_t i;
    size_t size;
    size_t count = 0;

    char *buffer = NULL;
    size_t offset = 0;

    if(data->configfd == -1) {
        data->configfd = open(path, O_RDWR); // open the config
        if(data->configfd < 0) {
            LOG(stderr, "open failed\n");
            LOG(stderr, "The config path is invalid!\n");
            err = -1;
            goto error_exit;
        }

        {
            struct flock lock;
            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;

            lock.l_start = 0;
            lock.l_len = 0;

            if(fcntl(data->configfd, F_SETLK, &lock)) { // try to lock the file
                LOG(stderr, "Config file locked! Another instance is already running!\n");
                err = -2;
                goto error_exit;
            }
        }
    } else {
        free(data->blacklist);
        data->blacklistsize = 0;
        lseek(data->configfd, 0, SEEK_SET);
    }

    data->maxtime.tv_sec = 0;
    data->maxtime.tv_nsec = 0;
    data->maxcount = -1;

    data->blacklistsize = 0;
    data->blacklist = NULL;

    size = readfile(data->configfd, &buffer);
    if(buffer == NULL) {
        LOG(stderr, "readfile failed\n");
        err = -3;
        goto error_exit;
    }

    for(i = 0; i < size - 1; i++) { // replace the ; delimiter with \0 to split string into multiple parts
        if(buffer[i] == ';') {
            buffer[i] = '\0';
            count++;
        }
        if(buffer[i] == '\n') {
            memmove_s(&buffer[i], MAX_BUFFER_SIZE, &buffer[i + 1], size - i - 1); // remove newlines
        }
    }

    for(i = 0; i < count; i++) {
        char *current = &buffer[offset]; // split string into lines with offset
        size_t length = strnlen_s(current, MAX_BUFFER_SIZE);

        if(strncmp_ss(current, "maxtime", 7) == 0 && data->maxtime.tv_sec == 0 &&
           data->maxtime.tv_nsec == 0) {
            char *end = NULL;
            data->maxtime.tv_sec = parse_longlong(&current[8], &end);
            data->maxtime.tv_nsec = parse_longlong(&end[1], &end);

            if(data->maxtime.tv_sec < 0 || data->maxtime.tv_nsec < 0) {
                LOG(stdout, "The option of maxtime is malformed or out of range!\n");
                err = -4;
                goto error_exit;
            }

            LOG(stdout, "Maxtime set to %lds %ldns\n", data->maxtime.tv_sec,
                data->maxtime.tv_nsec);

        } else if(strncmp_ss(current, "maxscore", 8) == 0 && data->maxcount == -1) {
            data->maxcount = parse_longlong(&current[9], NULL);

            if(data->maxcount < 0) {
                LOG(stdout, "The option of maxscore is malformed or out of range!\n");
                err = -5;
                goto error_exit;
            }

            LOG(stdout, "Maxscore set to %ld\n", data->maxcount);

        } else if(strncmp_ss(current, "blacklist", 9) == 0) {
            char *end = &current[9];

            while(true) {
                size_t number;

                number = parse_longlong(&end[1], &end);
                if(number <= KEY_MAX) {
                    size_t *temp;
                    data->blacklistsize++;

                    temp = realloc(data->blacklist, sizeof(size_t) * data->blacklistsize);
                    if(temp == NULL) {
                        LOG(stderr, "realloc failed\n");
                        err = -6;
                        goto error_exit;
                    }
                    data->blacklist = temp;

                    data->blacklist[data->blacklistsize - 1] = number;
                    LOG(stdout, "Added %d to the blacklist\n", number);

                } else {
                    LOG(stdout, "%d is not a valid keyboard scancode!\n");
                }

                if(*end == '\0') {
                    break;
                }
            }
        }
        offset += length + 1; // add size
    }

    free(buffer);
    return err;

    error_exit:
    if(close(data->configfd)) {
        LOG(stderr, "close failed\n");
    }
    free(data->blacklist);
    free(buffer);
    return err;
}

void handleargs(int argc, char *argv[], struct arg_data *data)
{
    size_t i;
    data->configpath[0] = '\0';
    daemonize = false;

    for(i = 1; i < argc; i++) {
        if(argv[i][0] == '-') {
            switch(argv[i][1]) {
                case 'c':
                    if(i + 1 < argc) {
                        strcpy_s(data->configpath, MAX_PATH_SIZE, argv[i + 1]); // config path
                    }
                    break;

                case 'l':
                    if(i + 1 < argc) {
                        strcpy_s(data->logpath, MAX_PATH_SIZE, argv[i + 1]); // log path (only usable when daemonized)
                    }
                    break;

                case 'd':
                    daemonize = true;
                    break;

                default:
                    printf("%s is not a recognized option\n", argv[i]);
                    break;
            }
        }
    }

    if(data->configpath[0] == '\0') {
        printf("Please provide a config location!\n");
        exit(EXIT_SUCCESS);
    }

    if(daemonize && data->logpath[0] == '\0') {
        printf("Please provide a log file when startet as a daemon!\n");
        exit(EXIT_SUCCESS);
    }
}

void _logger(FILE *fd, const char func[], const char format[], ...)
{
    va_list args;
    size_t appendedsize = strnlen_s(format, MAX_FORMAT_STRING_NAME) + 4;

    va_start(args, format);

    if(func != NULL) {
        char appended[appendedsize + strnlen_s(func, MAX_FUNCTION_NAME_SIZE)];
        sprintf(appended, "[%s] %s", func, format);
        vfprintf(fd, appended, args);
    } else {
        vfprintf(fd, format, args);
    }
    va_end(args);
}

// memsave strcmp functions
int strcmp_ss(const char str1[], const char str2[])
{
    size_t i = 0;

    while(str1[i] == str2[i]) {
        if(str1[i] == '\0' || str2[i] == '\0') {
            break;
        }
        i++;
    }

    return str1[i] - str2[i];
}

int strncmp_ss(const char str1[], const char str2[], size_t length)
{
    size_t i = 0;

    while(str1[i] == str2[i] && i >= length) {
        if(str1[i] == '\0' || str2[i] == '\0') {
            break;
        }
        i++;
    }

    return str1[i] - str2[i];
}
