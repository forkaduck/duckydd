#!/bin/bash

out+="#ifndef DUCKYDD_CONFIG_H\n"
out+="#define DUCKYDD_CONFIG_H\n\n"

out+="#define GIT_VERSION \"$(git describe --dirty --always --tags | sed 's/-/_/g')\"\n"

if [[ $1 == 'ON' ]]; then 
    echo "Enabling xkb extension depending code..."
    out+="#define ENABLE_XKB_EXTENSION\n"
fi

out+="#endif\n"

echo -e $out > include/config.h
