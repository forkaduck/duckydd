#!/bin/bash

vernum=$(git describe --dirty --always --tags)

out+="#ifndef DUCKYDD_CONFIG_H\n"
out+="#define DUCKYDD_CONFIG_H\n"

out+="#define GIT_VERSION \"$vernum\"\n"

out+="#endif\n"

echo -e $out > include/config.h
