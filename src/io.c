#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <inttypes.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <malloc.h>

#include "safe_lib.h"
#include "main.h"
#include "io.h"
#include "mbuffer.h"


#define PREFIX long_int
#define T long int
#include "mbuffertemplate.h"

#define PREFIX size_t
#define T size_t
#include "mbuffertemplate.h"


// config interpretation functions

static void cleaninput ( char * input, const size_t size )
{
        size_t i;

        for ( i = 0; i < size; i++ ) {
                char curr;

                curr = input[i];
                if ( ( curr <= '9' && curr >= '0' ) || ( curr <= 'Z' && curr >= 'A' ) || ( curr <= 'z' && curr >= 'a' ) || curr == '/' ) {
                        input[i] = curr;

                } else if ( curr == '\0' ) {
                        break;

                } else {
                        input[i] = ' ';
                }
        }
        input[i] = '\0';
}

static long int parse_longlong ( const char input[], char **end )
{
        long int num;
        errno = 0;

        num = strtol ( input, end, 0 );
        if ( end != NULL ) {
                if ( *end == input ) {
                        return -2;
                }
        }

        if ( ( num == LLONG_MAX || num == LLONG_MIN ) && errno == ERANGE ) {
                return -3;
        }

        return num;
}

static size_t readfile ( int fd, char **output )
{
        char buffer[10];
        size_t readsize = -1;
        size_t size = 0;

        while ( readsize != 0 ) { // read 10 bytes, reallocate the array and then copy them into the array
                char *temp;
                readsize = read ( fd, buffer, sizeof ( buffer ) );
                size += readsize;

                temp = realloc ( *output, sizeof ( char ) * size );
                if ( !temp ) {
                        free ( *output );
                        *output = NULL;
                        return 0;
                }
                *output = temp;

                memcpy_s ( & ( *output ) [size - readsize], readsize, buffer, readsize );
        }
        return size;
}

int readconfig ( const char path[], struct configInfo *data )
{
        int err = 0;
        size_t size;
        size_t usedsize;
        size_t i;

        char *buffer = NULL;
        char *current = NULL;

        data->maxtime.tv_sec = 0;
        data->maxtime.tv_nsec = 0;
        data->maxcount = -1;

        data->logpath[0] = '\0';

        init_mbuffer ( &data->blacklist, sizeof ( long int ) );
        data->logkeys = false;

        if ( data->configfd == -1 ) {
                data->configfd = open ( path, O_RDWR ); // open the config
                if ( data->configfd < 0 ) {
                        ERR ( "open" );
                        err = -1;
                        goto error_exit;
                }

                {
                        struct flock lock;
                        lock.l_type = F_WRLCK;
                        lock.l_whence = SEEK_SET;

                        lock.l_start = 0;
                        lock.l_len = 0;

                        if ( fcntl ( data->configfd, F_SETLK, &lock ) ) { // try to lock the file
                                if ( errno == EACCES || errno == EAGAIN ) {
                                        LOG ( -1, "Config file locked! Another instance is already running!\n" );
                                }
                                ERR ( "fcntl" );
                                err = -2;
                                goto error_exit;
                        }
                }
        } else {
                free_mbuffer ( &data->blacklist );
                lseek ( data->configfd, 0, SEEK_SET );
        }


        size = readfile ( data->configfd, &buffer );
        if ( buffer == NULL ) {
                LOG ( -1, "readfile failed\n" );
                err = -3;
                goto error_exit;
        }

        for ( i = 0; i < size; i++ ) {
                if ( buffer[i] == '\n' ) {
                        buffer[i] = '\0';
                }
        }


        current = buffer;
        usedsize = 0;
        while ( size > usedsize ) {
                size_t next;
                for ( next = 0; next < ( size - usedsize ); next++ ) {
                        if ( current[next] == '\0' ) {
                                usedsize += next + 1;
                                break;
                        }
                }
                cleaninput ( current, next );

                if ( strncmp_ss ( current, "maxtime", 7 ) == 0 && data->maxtime.tv_sec == 0 &&
                                data->maxtime.tv_nsec == 0 ) {
                        char *end = NULL;

                        data->maxtime.tv_sec = parse_longlong ( &current[8], &end );
                        data->maxtime.tv_nsec = parse_longlong ( &end[1], &end );

                        if ( data->maxtime.tv_sec < 0 || data->maxtime.tv_nsec < 0 ) {
                                LOG ( -1, "The option of maxtime is malformed or out of range!\n" );
                                err = -4;
                                goto error_exit;
                        }

                        LOG ( 1, "Maxtime set to %lds %ldns\n", data->maxtime.tv_sec,
                              data->maxtime.tv_nsec );

                } else if ( strncmp_ss ( current, "maxscore", 8 ) == 0 && data->maxcount == -1 ) {
                        data->maxcount = parse_longlong ( &current[9], NULL );

                        if ( data->maxcount < 0 ) {
                                LOG ( -1, "The option of maxscore is malformed or out of range!\n" );
                                err = -5;
                                goto error_exit;
                        }

                        LOG ( 1, "Maxscore set to %ld\n", data->maxcount );

                } else if ( strncmp_ss ( current, "blacklist", 8 ) == 0 ) {
                        char *end = &current[9];

                        long int number;

                        number = parse_longlong ( &end[1], &end );
                        if ( number <= KEY_MAX ) {
                                if ( append_mbuffer_member_long_int ( &data->blacklist, number ) ) {
                                        err = -6;
                                        goto error_exit;
                                }
                                LOG ( 1, "Added %d to the blacklist\n", number );

                        } else {
                                LOG ( 1, "%d is not a valid keyboard scancode!\n" );
                        }

                } else if ( strncmp_ss ( current, "logpath", 7 ) == 0 ) {
                        strcpy_s ( data->logpath, MAX_SIZE_PATH, &current[8] );

                        struct stat st;
                        if ( stat ( data->logpath, &st ) < 0 ) {
                                if ( ENOENT == errno ) {
                                        if ( mkdir ( data->logpath, 731 ) ) {
                                                ERR ( "mkdir" );
                                                err = -7;
                                                goto error_exit;
                                        }
                                } else {
                                        ERR ( "stat" );
                                        err = -8;
                                        goto error_exit;
                                }

                        } else {
                                if ( S_ISDIR ( st.st_mode ) ) {
                                        LOG ( 1, "Set %s as the path for logging!\n", data->logpath );
                                } else {
                                        LOG ( 0, "Logpath does not point to a directory!\n" );
                                        err = -9;
                                        goto error_exit;
                                }
                        }

                } else if ( strncmp_ss ( current, "keylogging", 10 ) == 0 ) {
                        if ( parse_longlong ( &current[11], NULL ) == 1 ) {
                                data->logkeys = true;
                                LOG ( 1, "Logging all potential attacks!\n" );
                        }
                }

                current = &buffer[usedsize];
        }

        free ( buffer );
        return err;

error_exit:
        if ( close ( data->configfd ) ) {
                ERR ( "close" );
        }
        free_mbuffer ( &data->blacklist );
        free ( buffer );
        return err;
}

int handleargs ( int argc, char *argv[], struct argInfo *data )
{
        size_t i;
        data->configpath[0] = '\0';

        for ( i = 1; i < argc; i++ ) {
                if ( argv[i][0] == '-' ) {
                        switch ( argv[i][1] ) {
                        case 'c':
                                if ( i + 1 < argc ) {
                                        strcpy_s ( data->configpath, MAX_SIZE_PATH, argv[i + 1] ); // config path
                                }
                                break;

                        case 'd':
                                daemonize = true;
                                break;

                        case 'v':
                                loglvl = 1;
                                break;

                        default:
                                printf ( "%s is not a recognized option\n", argv[i] );
                                break;
                        }
                }
        }

        if ( data->configpath[0] == '\0' ) {
                printf ( "Please provide a config location!\n" );
                return -1;
        }
        return 0;
}


void _logger ( short loglevel, const char func[], const char format[], ... )
{
        if ( loglevel <= loglvl ) {
                va_list args;
                va_start ( args, format );

                char appended[MAX_SIZE_FORMAT_STRING + MAX_SIZE_FUNCTION_NAME];
                char prefix;
                FILE *fd;

                switch ( loglevel ) {
                case -1:
                        prefix = '!';
                        fd = stderr;
                        break;

                default:
                        prefix = '*';
                        fd = stdout;
                        break;
                }

                if ( func != NULL ) {
                        sprintf ( appended, "[%c][%s] %s", prefix, func, format );
                        vfprintf ( fd, appended, args );
                } else {
                        sprintf ( appended, "[%c] %s", prefix, format );
                        vfprintf ( fd, format, args );
                }
                va_end ( args );
        }
}

// memsave strcmp functions
int strcmp_ss ( const char str1[], const char str2[] )
{
        size_t i = 0;

        while ( str1[i] == str2[i] ) {
                if ( str1[i] == '\0' || str2[i] == '\0' ) {
                        break;
                }
                i++;
        }

        return str1[i] - str2[i];
}

int strncmp_ss ( const char str1[], const char str2[], size_t length )
{
        size_t i = 0;

        while ( str1[i] == str2[i] && i >= length ) {
                if ( str1[i] == '\0' || str2[i] == '\0' ) {
                        break;
                }
                i++;
        }

        return str1[i] - str2[i];
}

const char *find_file ( const char *input )
{
        size_t i;

        for ( i = strnlen_s ( input, MAX_SIZE_PATH ); i > 0; i-- ) { // returns the filename
                if ( input[i] == '/' ) {
                        return &input[i + 1];
                }
        }
        return NULL;
}
