/*
 * This is a PoC that implements some ideas which should help protect against
 * rubber ducky attacks as a daemon
 *
 * TODO:
 *      * add attack logging
 *      * replace strcmp_ss with safelib implementation?
 *
 * TODO BUGS:
 *      * sometimes when grabbing the keyboard the last key that was pressed can get stuck
 *          and gets repeated until the devnode is removed
 * */


#ifndef __linux__
#warning This cant be compiled on a non posix compliant os!
#endif

#include <stdlib.h>
#include <stdbool.h>

#include <unistd.h>
#include <signal.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <bits/sigaction.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <linux/input.h>

#include <libudev.h>
#include <safe_lib.h>

#include "io.h"

#define MAX_EVENTS 40

bool brexit;
bool reloadconfig;

struct udev_data {
    int udevfd;
    struct udev *udev;
    struct udev_device *dev;
    struct udev_monitor *mon;
};

struct device {
    char *openfd;
    size_t openfdsize;
    struct timespec time_added;

    int score;
};

static void handle_signal(int signal)
{
    switch(signal) {
        case SIGINT:
        case SIGTERM:
            brexit = true; // exit cleanly
            break;

        case SIGHUP:
            reloadconfig = true; // reload config
            break;

        default:
            break;
    }
}

void become_daemon(struct arg_data data)
{
    int rv;

    // fork so that the child is not the process group leader
    rv = fork();
    if(rv > 0) {
        exit(EXIT_SUCCESS);
    } else if(rv < 0) {
        exit(EXIT_FAILURE);
    }

    // become a process group and session leader
    if(setsid() == -1) {
        exit(EXIT_FAILURE);
    }

    // fork so the child cant regain control of the controlling terminal
    rv = fork();
    if(rv > 0) {
        exit(EXIT_SUCCESS);
    } else if(rv < 0) {
        exit(EXIT_FAILURE);
    }

    // change base dir
    if(chdir("/")) {
        exit(EXIT_FAILURE);
    }

    // reset file mode creation mask
    umask(0);

    // close std file descriptors
    fclose(stdout);
    fclose(stdin);
    fclose(stderr);

    // log to a file
    freopen(data.logpath, "w", stdout);
    if(stdout < 0) {
        exit(EXIT_FAILURE);
    }

    freopen(data.logpath, "w", stderr);
    if(stderr < 0) {
        exit(EXIT_FAILURE);
    }
}

static int remove_fd(struct device **device_p, size_t *devicessize, const int epollfd, const char location[])
{
    int err = 0;
    int fd = -1;
    size_t i;
    size_t biggest = 0;

    for(i = 0; i < *devicessize; i++) { // find the fd in the array
        if((*device_p)[i].openfd != NULL && location != NULL) {
            if(strcmp_ss((*device_p)[i].openfd, location) == 0) {
                fd = i;
                break;
            }
        }
    }

    if(fd > 0) {
        if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL)) { // unregister the fd
            LOG(stderr, "epoll_ctl failed\n");
            err = -1;
            goto error_exit;
        }

        if(close(fd)) { // close the fd
            LOG(stderr, "close failed\n");
            err = -2;
            goto error_exit;
        }

        free((*device_p)[fd].openfd);
        (*device_p)[fd].openfd = NULL;

        for(i = 0; i < *devicessize; i++) { // find the biggest fd in the array
            if((*device_p)[i].openfd != NULL) {
                biggest = i;
            }
        }

        if(biggest < *devicessize) { // free the unnecessary space
            struct device *temp;

            *devicessize = biggest + 1;

            temp = realloc(*device_p, sizeof(struct device) * *devicessize);
            if(temp == NULL) {
                LOG(stderr, "realloc failed\n");
                err = -3;
                goto error_exit;
            }
            *device_p = temp;
        }
    } else {
        LOG(stderr, "did not find fd!\n");
    }

    return err;

    error_exit:
    if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL)) {
        LOG(stderr, "epoll_ctl failed\n");
    }

    if(close(fd)) {
        LOG(stderr, "close failed\n");
    }
    return err;
}

static int add_fd(struct device **device_p, size_t *devicessize, const int epollfd, const char location[])
{
    int err = 0;
    int fd;

    fd = open(location, O_RDONLY | O_NONBLOCK); // open a fd to the devnode
    if(fd == -1) {
        LOG(stderr, "open failed\n");
        err = -1;
        goto error_exit;
    }

    if(*devicessize <= fd) { // allocate more space if the fd doesnt fit
        size_t i;
        struct device *temp;
        size_t prevsize;

        prevsize = *devicessize;
        *devicessize = fd + 1;

        temp = realloc(*device_p, sizeof(struct device) * *devicessize);
        if(temp == NULL) {
            LOG(stderr, "realloc failed\n");
            err = -2;
            goto error_exit;
        }
        *device_p = temp;

        for(i = prevsize; i < *devicessize; i++) { // initialize all members of the device array which havent been used
            (*device_p)[i].openfd = NULL;
            (*device_p)[i].openfdsize = 0;
            (*device_p)[i].score = 0;

            if(clock_gettime(CLOCK_REALTIME, &((*device_p)[fd].time_added))) {
                LOG(stderr, "clock_gettime failed\n");
                (*device_p)[fd].time_added.tv_sec = 0;
                (*device_p)[fd].time_added.tv_nsec = 0;
            }
        }
    }

    if((*device_p)[fd].openfd == NULL) { // allocate and set the openfd
        (*device_p)[fd].openfdsize = strnlen_s(location, MAX_PATH_SIZE);
        (*device_p)[fd].openfd = malloc(sizeof(char) * (*device_p)[fd].openfdsize + 1);
        if((*device_p)[fd].openfd == NULL) {
            LOG(stderr, "malloc failed\n");
            err = -3;
            goto error_exit;
        }
        (*device_p)[fd].openfd[(*device_p)[fd].openfdsize] = '\0';

        memcpy_s((*device_p)[fd].openfd, (*device_p)[fd].openfdsize, location, (*device_p)[fd].openfdsize);

        {
            struct epoll_event event;
            memset_s(&event, sizeof(struct epoll_event), 0);
            event.events = EPOLLIN;
            event.data.fd = fd;

            if(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event)) {
                LOG(stderr, "epoll_fd failed\n");
                err = -4;
                goto error_exit;
            }
        }

    } else {
        LOG(stderr, "Cant add this fd because the place is already in use (impossible!!!)\n");
        err = -5;
        goto error_exit;
    }
    return fd;

    error_exit:
    if(close(fd)) {
        LOG(stderr, "close failed\n");
    }

    free((*device_p)[fd].openfd);
    return err;
}

static inline const char *find_file(const char *input)
{
    size_t i;

    for(i = strnlen_s(input, MAX_PATH_SIZE); i > 0; i--) { // returns the filename
        if(input[i] == '/') {
            return &input[i + 1];
        }
    }
    return NULL;
}

static int has_tty(struct udev_data data_udev)
{
    int err = 0;
    struct udev_enumerate *udev_enum;
    struct udev_list_entry *le;

    udev_enum = udev_enumerate_new(data_udev.udev);
    if(udev_enum == NULL) {
        LOG(stderr, "udev_enumerate_new failed\n");
        err = -1;
        goto error_exit;
    }

    if(udev_enumerate_add_match_subsystem(udev_enum, "tty") < 0) { // add match for "tty"
        LOG(stderr, "udev_enumerate_add_match_subsystem failed\n");
        err = -2;
        goto error_exit;
    }

    if(udev_enumerate_add_match_property(udev_enum, "ID_VENDOR_ID", // search for vendor id with tty
                                         udev_device_get_property_value(data_udev.dev, "ID_VENDOR_ID")) < 0) {
        LOG(stderr, "udev_enumerate_add_match_property failed\n");
        err = -3;
        goto error_exit;
    }

    udev_enumerate_scan_devices(udev_enum);

    le = udev_enumerate_get_list_entry(udev_enum); // loop through all entities and search for a usb tty
    while(le != NULL) {
        if(strncmp_ss(find_file(udev_list_entry_get_name(le)), "ttyACM", 6) == 0) {
            udev_enumerate_unref(udev_enum);
            return 1;
        }
        le = udev_list_entry_get_next(le);
    }
    udev_enumerate_unref(udev_enum);
    return 0;

    error_exit:
    udev_enumerate_unref(udev_enum);
    return err;
}

int main(int argc, char *argv[])
{
    size_t i;

    struct arg_data data_arg;
    struct udev_data data_udev;
    struct config_data data_config;

    struct device *device;
    size_t devicesize;

    int epollfd;
    struct epoll_event udevevent;

    // reset global variables
    brexit = false;
    reloadconfig = false;

    // handle non root
    if(getuid() != 0) {
        fprintf(stderr, "[%s] Please restart this daemon as root!\n", __func__);
        exit(EXIT_SUCCESS);
    }

    // interpret args
    handleargs(argc, argv, &data_arg);

    // daemonize if supplied
    if(daemonize) {
        become_daemon(data_arg);
    }

    // set stdout and stderr to non buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // read config
    data_config.configfd = -1;
    if(readconfig(data_arg.configpath, &data_config)) {
        LOG(stderr, "readconfig failed\n");
        exit(EXIT_FAILURE);
    }

    // INITIALIZE SIGNAL HANDLER
    {
        struct sigaction action;

        action.sa_handler = handle_signal;
        action.sa_flags = 0;
        sigemptyset(&action.sa_mask);

        if(sigaction(SIGHUP, &action, NULL)) {
            LOG(stderr, "sigaction failed (SIGHUP)\n");
            exit(EXIT_FAILURE);
        }

        if(sigaction(SIGTERM, &action, NULL)) {
            LOG(stderr, "sigaction failed (SIGTERM)\n");
            exit(EXIT_FAILURE);
        }

        if(sigaction(SIGINT, &action, NULL)) {
            LOG(stderr, "sigaction failed (SIGINT)\n");
            exit(EXIT_FAILURE);
        }

        action.sa_handler = SIG_IGN;
        if(sigaction(SIGCHLD, &action, NULL)) {
            LOG(stderr, "sigaction failed (SIGCHLD)\n");
            exit(EXIT_FAILURE);
        }
    }

    // INITIALIZE UDEV MONITOR
    {
        data_udev.udev = udev_new();
        if(data_udev.udev == NULL) {
            LOG(stderr, "udev_new failed\n");
            exit(EXIT_FAILURE);
        }

        data_udev.mon = udev_monitor_new_from_netlink(data_udev.udev, "udev");
        if(data_udev.mon == NULL) {
            LOG(stderr, "udev_monitor_new_from_netlink failed\n");
            exit(EXIT_FAILURE);
        }

        if(udev_monitor_filter_add_match_subsystem_devtype(data_udev.mon, "input", NULL) < 0) {
            LOG(stderr, "udev_monitor_filter_add_match_subsystem_devtype failed (input)\n");
            exit(EXIT_FAILURE);
        }

        if(udev_monitor_enable_receiving(data_udev.mon) < 0) {
            LOG(stderr, "udev_monitor_enable_receiving failed\n");
            exit(EXIT_FAILURE);
        }

        data_udev.udevfd = udev_monitor_get_fd(data_udev.mon);
        if(data_udev.udevfd == -1) {
            LOG(stderr, "udev_monitor_get_fd failed\n");
            exit(EXIT_FAILURE);
        }
    }

    // SETUP EPOLL
    epollfd = epoll_create(1); // size gets ignored since kernel 2.6.8
    if(epollfd < 0) {
        LOG(stderr, "epoll_create failed\n");
        exit(EXIT_FAILURE);
    }

    memset_s(&udevevent, sizeof(struct epoll_event), 0);
    udevevent.events = EPOLLIN;
    udevevent.data.fd = data_udev.udevfd;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, data_udev.udevfd, &udevevent)) {
        LOG(stderr, "epoll_ctl failed\n");
        exit(EXIT_FAILURE);
    }

    devicesize = 0;
    device = NULL;

    // MAIN LOOP
    while(!brexit) {
        int eth;
        struct epoll_event events[MAX_EVENTS];

        eth = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if(eth < 0) {
            if(errno == EINTR) { // fix endless loop when receiving SIGHUP
                eth = 0;

            } else {
                LOG(stderr, "epoll_wait failed\n");
                break;
            }
        }

        for(i = 0; i < eth; i++) {
            if(events[i].data.fd == data_udev.udevfd) { // if a new event device has been added
                const char *devnode;
                const char *action;

                data_udev.dev = udev_monitor_receive_device(data_udev.mon);
                if(data_udev.dev == NULL) {
                    LOG(stderr, "udev_monitor_receive_device failed\n");
                    exit(EXIT_FAILURE);
                }

                devnode = udev_device_get_devnode(data_udev.dev);
                action = udev_device_get_action(data_udev.dev);

                if(devnode != NULL && action != NULL) { // device has a devnode
                    /*LOG(stdout, "%s\n%s -> %s [%s] | %s:%s Ma:%s Mi:%s\n\n",
                        udev_device_get_devpath(data_udev.dev),
                        action, devnode, udev_device_get_subsystem(data_udev.dev),
                        udev_device_get_property_value(data_udev.dev, "ID_VENDOR_ID"),
                        udev_device_get_property_value(data_udev.dev, "ID_MODEL_ID"));
                    */

                    if(strncmp_ss(action, "add", 3) == 0) { // add the devnode to the array
                        int fd;

                        fd = add_fd(&device, &devicesize, epollfd, devnode);
                        if(fd >= 0) {
                            if(has_tty(data_udev)) {
                                device[fd].score++;
                            }
                        } else {
                            LOG(stderr, "add_fd failed\n");
                        }

                    } else if(strncmp_ss(action, "remove", 6) == 0) { // remove it
                        if(remove_fd(&device, &devicesize, epollfd, devnode)) {
                            LOG(stderr, "remove_fd failed\n");
                        }
                    }
                }
                udev_device_unref(data_udev.dev);

            } else {
                bool removed = false;
                int currentfd = events[i].data.fd;
                struct input_event event;

                memset_s(&event, sizeof(struct input_event), 0);

                // handle timeout
                if(currentfd < devicesize) {
                    if(device[currentfd].time_added.tv_sec != 0 && device[currentfd].time_added.tv_nsec != 0) {
                        struct timespec difference, current;
                        if(clock_gettime(CLOCK_REALTIME, &current)) {
                            LOG(stderr, "clock_gettime failed\n");
                            current.tv_sec = 0;
                            current.tv_nsec = 0;
                        }

                        difference.tv_sec = current.tv_sec - device[currentfd].time_added.tv_sec; // get time difference
                        difference.tv_nsec = current.tv_nsec - device[currentfd].time_added.tv_nsec;

                        // check for timeout
                        if(difference.tv_sec >= data_config.maxtime.tv_sec &&
                           difference.tv_nsec >= data_config.maxtime.tv_nsec) {
                            //TODO optimise to use fd instead of searching for the path
                            if(remove_fd(&device, &devicesize, epollfd, device[currentfd].openfd)) {
                                LOG(stderr, "remove_fd failed\n");
                            }
                            removed = true;
                            LOG(stdout, "%d timed out > %ds %dns timestamp:%d %d\n", currentfd, difference.tv_sec,
                                difference.tv_nsec, current.tv_sec, current.tv_nsec);
                        }
                    }

                    // handle keyboard grabbing
                    if(!removed) {
                        size_t size;

                        size = read(currentfd, &event, sizeof(struct input_event));
                        if(size > 0) {
                            if(event.type == EV_KEY) {
                                if(device[currentfd].score >= data_config.maxcount) {
                                    size_t k;
                                    bool handle = false;

                                    for(k = 0; k < data_config.blacklistsize; k++) {
                                        if(event.code == data_config.blacklist[k]) {
                                            handle = true;
                                        }
                                    }

                                    if(handle) {
                                        int s = 1;
                                        if(ioctl(currentfd, EVIOCGRAB, &s)) {
                                            LOG(stdout, "ioctl failed to grab keyboard\n");
                                        }
                                        device[currentfd].score = -1;
                                        LOG(stdout, "Locked fd %d\n", currentfd);
                                    }
                                }
                            }

                        } else {
                            LOG(stderr, "read failed\n");
                        }
                    }
                }
                events[i].events = 0;
            }
        }

        // reload config if SIGHUP is received
        if(reloadconfig) {
            LOG(stdout, "Reloading config file...\n");

            if(readconfig(data_arg.configpath, &data_config)) {
                LOG(stderr, "readconfig failed\n");
                exit(EXIT_FAILURE);
            }
            reloadconfig = false;
        }
    }

    // close all open file descriptors to event devnodes
    if(device != NULL) {
        for(i = 0; i < devicesize; i++) {
            if(device[i].openfd != NULL) {
                if(close(i)) {
                    LOG(stderr, "close failed\n");
                    exit(EXIT_FAILURE);
                }
                free(device[i].openfd);
            }
        }
        free(device);
    }
    if(close(data_config.configfd)) {
        LOG(stderr, "close failed (configfd)\n");
        exit(EXIT_FAILURE);
    }

    free(data_config.blacklist);

    if(close(epollfd)) {
        LOG(stderr, "close failed (epollfd)\n");
        exit(EXIT_FAILURE);
    }

    udev_monitor_unref(data_udev.mon);
    udev_unref(data_udev.udev);

    LOG(stdout, "Exiting!\n");

    exit(EXIT_SUCCESS);
}
