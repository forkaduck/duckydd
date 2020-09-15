
#include <stdbool.h>
#include <string.h>

#include <errno.h>
#include <signal.h>

#include "io.h"
#include "main.h"

void handle_signal(int signal)
{
    switch (signal) {
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

int init_signalhandler()
{
    struct sigaction action;

    action.sa_handler = handle_signal;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGHUP, &action, NULL)) {
        ERR("sigaction (SIGHUP)");
        return -1;
    }

    if (sigaction(SIGTERM, &action, NULL)) {
        ERR("sigaction (SIGTERM)");
        return -2;
    }

    if (sigaction(SIGINT, &action, NULL)) {
        ERR("sigaction (SIGINT)");
        return -3;
    }

    action.sa_handler = SIG_IGN;
    if (sigaction(SIGCHLD, &action, NULL)) {
        ERR("sigaction (SIGCHLD)");
        return -4;
    }

    return 0;
}
