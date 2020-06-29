/*
 * This is a PoC that implements some ideas which should help protect against
 * rubber ducky attacks as a daemon
 *
 * TODO:
 * 		Maybe?
 * 		* implement dynamic epoll_wait timeout by checking the next device timeout?
 *      * replace strcmp_ss with safelib implementation?
 * 		* write unit tests with cmocka?
 *
 * TODO BUGS:
 * */


#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>
#include <signal.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <bits/sigaction.h>
#include <sys/epoll.h>
#include <linux/input.h>
#include <libudev.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "safe_lib.h"
#include "io.h"
#include "main.h"
#include "daemon.h"
#include "udev.h"
#include "signalhandler.h"
#include "mbuffer.h"
#include "logkeys.h"

#define PREFIX char
#define T char
#include "mbuffertemplate.h"

static int deinit_device ( struct device *device, struct configInfo *config, struct keyboardInfo *kbd, const int epollfd )
{
        int err = 0;

        if ( device->fd != -1 && device->openfd[0] != '\0' ) {
                if ( epoll_ctl ( epollfd, EPOLL_CTL_DEL, device->fd, NULL ) ) { // unregister the fd
                        ERR ( "epoll_ctl" );
                        err = -1;
                        goto error_exit;
                }

                if ( close ( device->fd ) ) { // close the fd
                        ERR ( "close" );
                        err = -2;
                        goto error_exit;
                }

                device->openfd[0] = '\0';
                device->fd = -1;
        }


        if ( config->logkeys && device->state != NULL ) {
                if ( device->devlog.size > 0 ) {
                        if ( append_mbuffer_array_char ( & device->devlog, "\n\0", 2 ) ) {
                                LOG ( -1, "append_mbuffer_array_char failed!\n" );
                        }
                        LOG ( 1, "-> %s", device->devlog.b );

                        if ( write ( kbd->outfd, ( char * ) device->devlog.b, device->devlog.size ) < 0 ) {
                                ERR ( "write" );
                        }

                        free_mbuffer ( & device->devlog );
                }

                xkb_state_unref ( device->state );
                device->state = NULL;
        }
        return err;

error_exit:
        if ( close ( device->fd ) ) {
                LOG ( -1, "close failed\n" );
        }
        return err;
}

static int search_fd ( struct device **device, size_t *devicesize, const char location[] )
{
        LOG ( 1, "Searching for: %s\n", location );

        if ( location != NULL ) {
                size_t i;

                for ( i = 0; i < *devicesize; i++ ) { // find the fd in the array
                        if ( strcmp_ss ( ( *device ) [i].openfd, location ) == 0 && ( *device ) [i].fd != -1 ) {
                                return i;
                        }
                }
        }
        return -1;
}

static int remove_fd ( struct device **device, size_t *devicesize, struct configInfo *config, struct keyboardInfo *kbd, const int epollfd, const int fd )
{
        if ( fd > -1 ) {
                if ( deinit_device ( & ( *device ) [fd], config, kbd, epollfd ) ) {
                        LOG ( -1, "deinit_device failed! Memory leak possible!\n" );
                        return -1;
                }

                {
                        size_t i;
                        size_t bigsize = 0;

                        for ( i = 0; i < *devicesize; i++ ) { // find the biggest fd in the array
                                if ( ( *device ) [i].openfd[0] != '\0' ) {
                                        bigsize = i +1;
                                }
                        }

                        if ( bigsize < *devicesize ) { // free the unnecessary space
                                struct device *temp;
                                bool failed = false;

                                for ( i = bigsize; i < *devicesize; i++ ) {
                                        if ( deinit_device ( & ( *device ) [i], config, kbd, epollfd ) ) {
                                                LOG ( -1, "deinit_device failed! Memory leak possible!\n" );
                                                failed = true;
                                        }
                                }

                                if ( !failed ) {
                                        temp = realloc ( *device, sizeof ( struct device ) * bigsize );
                                        if ( temp == NULL && bigsize != 0 ) {
                                                ERR ( "realloc" );

                                        } else {
                                                *devicesize = bigsize;
                                                *device = temp;
                                        }
                                }

                        }
                }

        } else {
                LOG ( -1, "Did not find %d\n", fd );
                return -2;
        }

        LOG ( 1, "Removed %d\n\n", fd );

        return 0;
}

static int add_fd ( struct device **device, size_t *devicesize, struct keyboardInfo *kbd, struct configInfo *config, const int epollfd, const char location[] )
{
        int err = 0;
        int fd;

        LOG ( 1, "Adding for: %s\n", location );

        fd = open ( location, O_RDONLY | O_NONBLOCK ); // open a fd to the devnode
        if ( fd == -1 ) {
                LOG ( -1, "open failed\n" );
                err = -1;
                goto error_exit;
        }

        if ( *devicesize <= fd ) { // allocate more space if the fd doesnt fit
                size_t i;
                struct device *temp;
                size_t prevsize;

                prevsize = *devicesize;
                *devicesize = fd + 1;

                temp = realloc ( *device, sizeof ( struct device ) * *devicesize );
                if ( temp == NULL ) {
                        ERR ( "realloc" );
                        err = -2;
                        goto error_exit;
                }
                *device = temp;

                for ( i = prevsize; i < *devicesize; i++ ) { // initialize all members of the device array which havent been used
                        ( *device ) [i].openfd[0] = '\0';
                        ( *device ) [i].fd = -1;

                        ( *device ) [i].score = 0;
                        ( *device ) [i].state = NULL;
                }
        }

        if ( ( *device ) [fd].openfd[0] == '\0' && ( *device ) [fd].fd == -1 ) { // allocate and set the openfd
                strcpy_s ( ( *device ) [fd].openfd, MAX_SIZE_PATH, location );
                ( *device ) [fd].fd = fd;

                init_mbuffer ( & ( *device ) [fd].devlog, sizeof ( char ) );

                if ( clock_gettime ( CLOCK_REALTIME, & ( *device ) [fd].time_added ) ) {
                        ERR ( "clock_gettime" );
                        ( *device ) [fd].time_added.tv_sec = 0;
                        ( *device ) [fd].time_added.tv_nsec = 0;
                }

                if ( config->logkeys ) {
                        ( *device ) [fd].state = xkb_x11_state_new_from_device ( kbd->keymap, kbd->con, kbd->device_id );
                        if ( ! ( *device ) [fd].state ) {
                                LOG ( -1, "xkb_x11_state_new_from_device failed!\n" );
                                return -1;
                        }
                }

                {
                        struct epoll_event event;
                        memset_s ( &event, sizeof ( struct epoll_event ), 0 );
                        event.events = EPOLLIN;
                        event.data.fd = fd;

                        if ( epoll_ctl ( epollfd, EPOLL_CTL_ADD, fd, &event ) ) {
                                ERR ( "epoll_ctl" );
                                err = -4;
                                goto error_exit;
                        }
                }

        } else {
                LOG ( -1, "Somehow this fd element is already in use! This is a bug and should not happen!\n" );
                err = -5;
                goto error_exit;
        }
        LOG ( 1, "Added %i\n\n", fd );
        return fd;

error_exit:
        if ( close ( fd ) ) {
                LOG ( -1, "close failed\n" );
        }
        return err;
}

static int handle_udevev ( struct device **device, size_t *devicesize, struct keyboardInfo *kbd, struct configInfo *config, struct udevInfo *udev, const int epollfd )
{
        int err = 0;
        const char *devnode;
        const char *action;

        udev->dev = udev_monitor_receive_device ( udev->mon );
        if ( udev->dev == NULL ) {
                LOG ( -1, "udev_monitor_receive_device failed\n" );
                err = -1;
                goto error_exit;
        }

        devnode = udev_device_get_devnode ( udev->dev );
        action = udev_device_get_action ( udev->dev );

        if ( devnode != NULL && action != NULL ) { // device has a devnode
                LOG ( 1, "%s\n%s -> %s [%s] | %s:%s\n",
                      udev_device_get_devpath ( udev->dev ),
                      action, devnode, udev_device_get_subsystem ( udev->dev ),
                      udev_device_get_property_value ( udev->dev, "MAJOR" ),
                      udev_device_get_property_value ( udev->dev, "MINOR" ) );

                if ( strncmp_ss ( action, "add", 3 ) == 0 ) { // add the devnode to the array
                        int fd;

                        fd = add_fd ( device, devicesize, kbd, config, epollfd, devnode );
                        if ( fd >= 0 ) {
                                if ( has_tty ( *udev ) ) {
                                        ( *device ) [fd].score++;
                                }
                        } else {
                                LOG ( -1, "add_fd failed\n" );
                                err = -2;
                                goto error_exit;
                        }

                } else if ( strncmp_ss ( action, "remove", 6 ) == 0 ) { // remove it
                        int fd;

                        fd = search_fd ( device, devicesize, devnode );
                        if ( fd >= 0 ) {
                                if ( remove_fd ( device, devicesize, config, kbd, epollfd, fd ) ) {
                                        LOG ( -1, "remove_fd failed\n" );
                                        err = -4;
                                        goto error_exit;
                                }
                        }
                }
        }

error_exit:
        udev_device_unref ( udev->dev );
        return err;
}


int main ( int argc, char *argv[] )
{
        size_t i;

        struct argInfo arg;
        struct udevInfo udev;
        struct configInfo config;
        struct keyboardInfo kbd;

        struct device *device;
        size_t devicesize;

        int epollfd;
        struct epoll_event udevevent;

        // reset global variables
        brexit = false;
        reloadconfig = false;
        daemonize = false;
        loglvl = 0;

        // handle non root
        if ( getuid() != 0 ) {
                LOG ( -1, "Please restart this daemon as root!\n" );
                return -1;
        }

        // interpret args
        if ( handleargs ( argc, argv, &arg ) ) {
                LOG ( -1, "handleargs failed!\n" );
                return -1;
        }

        // read config
        config.configfd = -1;
        if ( readconfig ( arg.configpath, &config ) ) {
                LOG ( -1, "readconfig failed\n" );
                return -1;
        }
        // daemonize if supplied
        if ( daemonize ) {
                if ( become_daemon ( config ) ) {
                        LOG ( -1, "become_daemon failed!\n" );
                        return -1;
                }
        }

        // set 0 and -1 to non buffering
        setbuf ( stdout, NULL );
        setbuf ( stderr, NULL );


        // init signal handler
        if ( init_signalhandler ( config ) ) {
                LOG ( -1, "init_sighandler failed\n" );
                return -1;
        }

        // init the udev monitor
        if ( init_udev ( &udev ) ) {
                LOG ( -1, "init_udev failed\n" );
                return -1;
        }

        // init keylogging if supplied
        if ( config.logkeys ) {
                if ( init_keylogging ( NULL, &kbd, &config ) ) {
                        LOG ( -1, "init_keylogging failed\n" );
                }
        }


        // SETUP EPOLL
        epollfd = epoll_create ( 1 ); // size gets ignored since kernel 2.6.8
        if ( epollfd < 0 ) {
                ERR ( "epoll_create" );
                return -1;
        }

        memset_s ( &udevevent, sizeof ( struct epoll_event ), 0 );
        udevevent.events = EPOLLIN;
        udevevent.data.fd = udev.udevfd;

        if ( epoll_ctl ( epollfd, EPOLL_CTL_ADD, udev.udevfd, &udevevent ) ) {
                ERR ( "epoll_ctl" );
                return -1;
        }

        devicesize = 0;
        device = NULL;

        // MAIN LOOP
        while ( !brexit ) {
                int eth;
                struct epoll_event events[MAX_SIZE_EVENTS];

                eth = epoll_wait ( epollfd, events, MAX_SIZE_EVENTS, -1 );
                if ( eth < 0 ) {
                        if ( errno == EINTR ) { // fix endless loop when receiving SIGHUP
                                eth = 0;

                        } else {
                                ERR ( "epoll_wait" );
                                break;
                        }
                }

                for ( i = 0; i < eth; i++ ) {
                        int fd = events[i].data.fd;

                        if ( ( events[i].events & EPOLLIN ) > 0 ) {
                                if ( fd == udev.udevfd ) { // if a new event device has been added
                                        if ( handle_udevev ( &device, &devicesize, &kbd, &config, &udev, epollfd ) ) {
                                                LOG ( -1, "handle_udevev failed!\n" );
                                        }

                                } else {
                                        struct input_event event;
                                        int16_t size;

                                        size = read ( fd, &event, sizeof ( struct input_event ) );
                                        if ( size < 0 ) {
                                                ERR ( "read" );

                                        } else {
                                                // handle keyboard grabbing
                                                if ( event.type == EV_KEY ) {
                                                        if ( device[fd].score >= config.maxcount) {
                                                                size_t k;
                                                                bool handle = false;

                                                                for ( k = 0; k < config.blacklist.size; k++ ) {
                                                                        if ( event.code == ( ( long int * ) config.blacklist.b ) [k] ) {
                                                                                handle = true;
                                                                        }
                                                                }
                                                                
																LOG(1, "fd=%d event.type=%d event.code=%d event.value=%d\n",fd, event.type, event.code, event.value);
                                                                if ( handle && event.value == 0 ) {
                                                                        if ( ioctl ( fd, EVIOCGRAB, 1 ) ) {
                                                                                ERR ( "ioctl" );
                                                                        }
                                                                        device[fd].score = -1;
                                                                        LOG ( 0, "Locked fd %d\n", fd );
                                                                }
                                                        }

                                                        if ( config.logkeys ) {
                                                                if ( logkey ( &kbd, &device[fd], event ) ) {
                                                                        LOG ( 0, "logkey failed!\n" );
                                                                }
                                                        }
                                                }
                                                else if ( event.type == SYN_DROPPED ) {
														LOG(-1, "Sync dropped! Eventhandler not fast enough!\n");
												}


                                                // handle timeout
                                                if ( device[fd].time_added.tv_sec != 0 && device[fd].time_added.tv_nsec != 0 ) {
                                                        struct timespec diff, curr;
                                                        if ( clock_gettime ( CLOCK_REALTIME, &curr ) ) {
                                                                ERR ( "clock_gettime" );
                                                                curr.tv_sec = 0;
                                                                curr.tv_nsec = 0;
                                                        }

                                                        diff.tv_sec = curr.tv_sec - device[fd].time_added.tv_sec; // get time difference
                                                        diff.tv_nsec = curr.tv_nsec - device[fd].time_added.tv_nsec;

                                                        // check for timeout
                                                        if ( diff.tv_sec >= config.maxtime.tv_sec &&
                                                                        diff.tv_nsec >= config.maxtime.tv_nsec ) {
                                                                if ( remove_fd ( &device, &devicesize, &config, &kbd, epollfd, fd ) ) {
                                                                        LOG ( -1, "remove_fd failed\n" );
                                                                }
                                                                LOG ( 0, "%d timed out > %ds %dns timestamp:%d %d\n", fd, diff.tv_sec,
                                                                      diff.tv_nsec, curr.tv_sec, curr.tv_nsec );
                                                        }
                                                }
                                        }
                                }
                        }
                        events[i].events = 0;
                }

                // reload config if SIGHUP is received
                if ( reloadconfig ) {
                        LOG ( 0, "Reloading config file...\n" );

                        if ( readconfig ( arg.configpath, &config ) ) {
                                LOG ( -1, "readconfig failed\n" );
                                return -1;
                        }
                        reloadconfig = false;
                }
        }

        // close all open file descriptors to event devnodes
        if ( device != NULL ) {
                for ( i = 0; i < devicesize; i++ ) {
                        if ( device[i].openfd[0] != '\0' && device[i].fd != -1 ) {
                                LOG ( 1, "fd %d still open!\n", i );
                                if ( deinit_device ( &device[i], &config, &kbd, epollfd ) ) {
                                        LOG ( -1, "deinit_device failed!\n" );
                                }
                        }
                }
                free ( device );
        }

        if ( close ( config.configfd ) ) {
                ERR ( "close" );
                return -1;
        }

        free_mbuffer ( &config.blacklist );

        if ( close ( epollfd ) ) {
                ERR ( "close" );
                return -1;
        }

        deinit_udev ( &udev );

        if ( config.logkeys ) {
                deinit_keylogging ( &kbd );
        }
        LOG ( 0, "Exiting!\n" );

        return 0;
}
