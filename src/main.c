/*
 * This is a PoC that implements some ideas which should help protect against
 * rubber ducky attacks as a daemon
 * */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <libudev.h>
#include <limits.h>
#include <linux/input.h>
#include <malloc.h>
#include <math.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "daemon.h"
#include "io.h"
#include "logkeys.h"
#include "mbuffer.h"
#include "safe_lib.h"
#include "signalhandler.h"
#include "udev.h"

#if _POSIX_TIMERS <= 0
#error "Can't use posix time functions!"
#endif

#define PREFIX char
#define T char
#include "mbuffertemplate.h"

#define PREFIX deviceInfo
#define T struct deviceInfo
#include "mbuffertemplate.h"

#define PREFIX struct_timespec
#define T struct timespec
#include "mbuffertemplate.h"

// global variables
bool brexit;
bool reloadconfig;
bool daemonize;
short loglvl;

static int deinit_device(struct deviceInfo* device, struct configInfo* config, struct keyboardInfo* kbd, const int epollfd)
{
    int err = 0;

    if (device->fd != -1) {
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, device->fd, NULL)) { // unregister the fd
            ERR("epoll_ctl");
            err = -1;
            goto error_exit;
        }

        if (close(device->fd)) { // close the fd
            ERR("close");
            err = -2;
            goto error_exit;
        }

        device->openfd[0] = '\0';
        device->fd = -1;
    }

    if (device->devlog.b != NULL) {
        if (device->score < 0) {
            if (m_append_array_char(&device->devlog, "\n\0", 2)) {
                LOG(-1, "append_mbuffer_array_char failed!\n");
            }

            // write keylog to the log file
            if (write(kbd->outfd, (char*)device->devlog.b, device->devlog.size) < 0) {
                ERR("write");
            }
        }
        m_free(&device->devlog);
    }

    if (config->xkeymaps && device->xstate != NULL) {
        xkb_state_unref(device->xstate);
        device->xstate = NULL;
    }

    m_free(&device->strokesdiff);
    return err;

error_exit:
    if (close(device->fd)) {
        LOG(-1, "close failed\n");
    }
    return err;
}

static int search_fd(struct managedBuffer* device, const char location[])
{
    LOG(1, "Searching for: %s\n", location);

    if (location != NULL) {
        size_t i;

        for (i = 0; i < device->size; i++) { // find the fd in the array
            if (strcmp_ss(m_deviceInfo(device)[i].openfd, location) == 0 && m_deviceInfo(device)[i].fd != -1) {
                return i;
            }
        }
    }
    return -1;
}

static int remove_fd(struct managedBuffer* device, struct configInfo* config, struct keyboardInfo* kbd, const int epollfd, const int fd)
{
    if (fd > -1) {
        if (deinit_device(&m_deviceInfo(device)[fd], config, kbd, epollfd)) {
            LOG(-1, "deinit_device failed! Memory leak possible!\n");
            return -1;
        }

        {
            size_t i;
            size_t bigsize = 0;

            // find the biggest fd in the array
            for (i = 0; i < device->size; i++) {
                if (m_deviceInfo(device)[i].openfd[0] != '\0') {
                    bigsize = i + 1;
                }
            }

            // free the unnecessary space
            if (bigsize < device->size) {
                bool failed = false;

                for (i = bigsize; i < device->size; i++) {
                    if (deinit_device(&m_deviceInfo(device)[i], config, kbd, epollfd)) {
                        LOG(-1, "deinit_device failed! Memory leak possible!\n");
                        failed = true;
                    }
                }

                if (!failed) {
                    if (m_realloc(device, bigsize)) {
                        LOG(-1, "m_realloc failed!\n");
                    }
                }
            }
        }

    } else {
        LOG(1, "Did not find %d\n", fd);
        return -2;
    }

    LOG(1, "Removed %d\n\n", fd);

    return 0;
}

static int add_fd(struct managedBuffer* device, struct keyboardInfo* kbd, struct configInfo* config, const int epollfd, const char location[])
{
    int err = 0;
    int fd;

    LOG(1, "Adding: %s\n", location);

    // open a fd to the devnode
    fd = open(location, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        LOG(-1, "open failed\n");
        err = -1;
        goto error_exit;
    }

    // allocate more space if the fd doesn't fit
    if (device->size <= fd) {
        size_t i;
        size_t prevsize;

        prevsize = device->size;
        if (m_realloc(device, fd + 1)) {
            LOG(-1, "m_realloc failed!\n");
            err = -2;
            goto error_exit;
        }

        // initialize all members of the device array which haven't been used
        for (i = prevsize; i < device->size; i++) {
            m_deviceInfo(device)[i].openfd[0] = '\0';
            m_deviceInfo(device)[i].fd = -1;

            m_deviceInfo(device)[i].score = 0;
            m_deviceInfo(device)[i].xstate = NULL;
            m_init(&m_deviceInfo(device)[i].devlog, sizeof(char));


            m_init(&m_deviceInfo(device)[i].strokesdiff, sizeof(struct timespec));
            
            m_deviceInfo(device)[i].lasttime.tv_sec = 0;
            m_deviceInfo(device)[i].lasttime.tv_nsec = 0;
            m_deviceInfo(device)[i].currdiff = 0;
        }
    }

    // allocate and set the openfd
    if (m_deviceInfo(device)[fd].fd == -1) {
        size_t i;

        strcpy_s(m_deviceInfo(device)[fd].openfd, MAX_SIZE_PATH, location);
        m_deviceInfo(device)[fd].fd = fd;

        if (config->xkeymaps) {
            // set the device state
            m_deviceInfo(device)[fd].xstate = xkb_x11_state_new_from_device(kbd->x.keymap, kbd->x.con, kbd->x.device_id);
            if (!m_deviceInfo(device)[fd].xstate) {
                LOG(-1, "xkb_x11_state_new_from_device failed!\n");
                err = -3;
                goto error_exit;
            }
        }

        // allocate the array
        if (m_realloc(&m_deviceInfo(device)[fd].strokesdiff, 6)) {
            LOG(-1, "m_realloc failed!\n");
            err = -4;
            goto error_exit;
        }

        // set every member of the strokediff array to 0
        for (i = 0; i < m_deviceInfo(device)[fd].strokesdiff.size; i++) {
            m_struct_timespec(&m_deviceInfo(device)[fd].strokesdiff)[i].tv_sec = 0;
            m_struct_timespec(&m_deviceInfo(device)[fd].strokesdiff)[i].tv_nsec = 0;
        }

        // add a new fd which should be polled
        {
            struct epoll_event event;
            memset_s(&event, sizeof(struct epoll_event), 0);
            event.events = EPOLLIN;
            event.data.fd = fd;

            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event)) {
                ERR("epoll_ctl");
                err = -5;
                goto error_exit;
            }
        }
    } else {
        LOG(-1, "Device fd already in use! There is something seriously wrong!\n");
        err = -5;
        goto error_exit;
    }
    LOG(1, "Added %i\n\n", fd);
    return fd;

error_exit:
    if (close(fd)) {
        LOG(-1, "close failed\n");
    }
    return err;
}

static int handle_udevev(struct managedBuffer* device, struct keyboardInfo* kbd, struct configInfo* config, struct udevInfo* udev, const int epollfd)
{
    int err = 0;
    const char* devnode;
    const char* action;

    // get a device context from the monitor
    udev->dev = udev_monitor_receive_device(udev->mon);
    if (udev->dev == NULL) {
        LOG(-1, "udev_monitor_receive_device failed\n");
        err = -1;
        goto error_exit;
    }

    devnode = udev_device_get_devnode(udev->dev);
    action = udev_device_get_action(udev->dev);

    // device has a devnode
    if (devnode != NULL && action != NULL) {
        LOG(2, "%s %s -> %s [%s] | %s:%s\n",
            udev_device_get_devpath(udev->dev),
            action, devnode, udev_device_get_subsystem(udev->dev),
            udev_device_get_property_value(udev->dev, "MAJOR"),
            udev_device_get_property_value(udev->dev, "MINOR"));

        // add the devnode to the array
        if (strncmp_ss(action, "add", 3) == 0) {
            int fd;

            fd = add_fd(device, kbd, config, epollfd, devnode);
            if (fd >= 0) {
                if (has_tty(*udev)) {
                    m_deviceInfo(device)[fd].score++;
                }
            } else {
                LOG(-1, "add_fd failed\n");
                err = -2;
                goto error_exit;
            }

            // remove the fd from the device array
        } else if (strncmp_ss(action, "remove", 6) == 0) {
            int fd;

            // search for the fd and remove it if possible
            fd = search_fd(device, devnode);
            if (fd >= 0) {
                if (remove_fd(device, config, kbd, epollfd, fd)) {
                    LOG(-1, "remove_fd failed\n");
                    err = -3;
                    goto error_exit;
                }
            }
        }
    }

error_exit:
    udev_device_unref(udev->dev);
    return err;
}

int main(int argc, char* argv[])
{
    size_t i;

    struct argInfo arg;
    struct udevInfo udev;
    struct configInfo config;
    struct keyboardInfo kbd;

    struct managedBuffer device;

    int epollfd;
    struct epoll_event udevevent;

    // reset global variables
    brexit = false;
    reloadconfig = false;
    daemonize = false;
    loglvl = 0;

    // handle non root
    if (getuid() != 0) {
        LOG(-1, "Please restart this daemon as root!\n");
        return -1;
    }

    // set coredump limit
    {
        struct rlimit limit;
        limit.rlim_cur = 0;
        limit.rlim_max = 0;

        setrlimit(RLIMIT_CORE, &limit);
    }

    // init device managed buffer
    m_init(&device, sizeof(struct deviceInfo));

    // interpret args
    if (handleargs(argc, argv, &arg)) {
        LOG(-1, "handleargs failed!\n");
        return -1;
    }

    // read config
    config.configfd = -1;
    if (readconfig(arg.configpath, &config)) {
        LOG(-1, "readconfig failed\n");
        return -1;
    }
    // daemonize if supplied
    if (daemonize) {
        if (become_daemon(config)) {
            LOG(-1, "become_daemon failed!\n");
            return -1;
        }
    }

    // set 0 and -1 to non buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // init signal handler
    if (init_signalhandler(config)) {
        LOG(-1, "init_sighandler failed\n");
        return -1;
    }

    // init the udev monitor
    if (init_udev(&udev)) {
        LOG(-1, "init_udev failed\n");
        return -1;
    }

    // init keylogging if supplied
    if (init_keylogging(NULL, &kbd, &config)) {
        LOG(-1, "init_keylogging failed\n");
    }

    // SETUP EPOLL
    epollfd = epoll_create(1); // size gets ignored since kernel 2.6.8
    if (epollfd < 0) {
        ERR("epoll_create");
        return -1;
    }

    memset_s(&udevevent, sizeof(struct epoll_event), 0);
    udevevent.events = EPOLLIN;
    udevevent.data.fd = udev.udevfd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, udev.udevfd, &udevevent)) {
        ERR("epoll_ctl");
        return -1;
    }
    LOG(0, "Startup done!\n");

    // MAIN LOOP
    while (!brexit) {
        int readfds;
        struct epoll_event events[MAX_SIZE_EVENTS];

        readfds = epoll_wait(epollfd, events, MAX_SIZE_EVENTS, -1);
        if (readfds < 0) {
            if (errno == EINTR) { // fix endless loop when receiving SIGHUP
                readfds = 0;

            } else {
                ERR("epoll_wait");
                break;
            }
        }

        for (i = 0; i < readfds; i++) {
            int fd = events[i].data.fd;

            if ((events[i].events & EPOLLIN) > 0) {
                if (fd == udev.udevfd) { // if a new event device has been added
                    if (handle_udevev(&device, &kbd, &config, &udev, epollfd)) {
                        LOG(-1, "handle_udevev failed!\n");
                    }

                } else {
                    struct input_event event;
                    int16_t size;

                    size = read(fd, &event, sizeof(struct input_event));
                    if (size < 0) {
                        ERR("read");

                    } else {
                        // handle keyboard grabbing
                        if (event.type == EV_KEY) {
                            LOG(2, "fd=%d event.type=%d event.code=%d event.value=%d\n", fd, event.type, event.code, event.value);

                            if (m_deviceInfo(&device)[fd].score >= config.maxcount) {
                                if (event.value == 0) {
                                    int ioctlarg = 1;
                                    if (ioctl(fd, EVIOCGRAB, &ioctlarg)) {
                                        ERR("ioctl");
                                    }
                                    m_deviceInfo(&device)[fd].score = -1;
                                    LOG(0, "Locked fd %d\n", fd);
                                }
                            }

                            if (logkey(&kbd, &m_deviceInfo(&device)[fd], event, &config)) {
                                LOG(0, "logkey failed!\n");
                            }
                        } else if (event.type == SYN_DROPPED) {
                            LOG(-1, "Sync dropped! Eventhandler not fast enough!\n");
                        }
                    }
                }
            }
            events[i].events = 0;
        }

        // reload config if SIGHUP is received
        if (reloadconfig) {
            LOG(0, "Reloading config file...\n");

            if (readconfig(arg.configpath, &config)) {
                LOG(-1, "readconfig failed\n");
                return -1;
            }
            reloadconfig = false;
        }
    }

    // close all open file descriptors to event devnodes
    if (m_deviceInfo(&device) != NULL) {
        for (i = 0; i < device.size; i++) {
            if (m_deviceInfo(&device)[i].fd != -1) {
                LOG(1, "fd %d still open!\n", i);
                if (deinit_device(&m_deviceInfo(&device)[i], &config, &kbd, epollfd)) {
                    LOG(-1, "deinit_device failed!\n");
                }
            }
        }
        m_free(&device);
    }

    if (close(config.configfd)) {
        ERR("close");
        return -1;
    }

    if (close(epollfd)) {
        ERR("close");
        return -1;
    }

    deinit_udev(&udev);

    deinit_keylogging(&kbd, &config);

    LOG(0, "All exit routines done!\n");
    return 0;
}
