#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <unistd.h>

#include "io.h"
#include "safe_lib.h"

int become_daemon(struct configInfo config)
{
    int rv;

    // fork so that the child is not the process group leader
    rv = fork();
    if (rv > 0) {
        exit(EXIT_SUCCESS);
    } else if (rv < 0) {
        exit(EXIT_FAILURE);
    }

    // become a process group and session leader
    if (setsid() == -1) {
        exit(EXIT_FAILURE);
    }

    // fork so the child cant regain control of the controlling terminal
    rv = fork();
    if (rv > 0) {
        exit(EXIT_SUCCESS);
    } else if (rv < 0) {
        exit(EXIT_FAILURE);
    }

    // change base dir
    if (chdir("/")) {
        return -1;
    }

    // reset file mode creation mask
    umask(0);

    // close std file descriptors
    fclose(stdout);
    fclose(stdin);
    fclose(stderr);

    {
        char path[PATH_MAX] = {'\0'};

        pathcpy(path, config.logpath);
        pathcat(path, "/out.log");

        // log to a file
        if (freopen(path, "w", stdout) != stdout) {
            return -2;
        }

        if (freopen(path, "w", stderr) != stderr) {
            return -3;
        }
    }
    return 0;
}
