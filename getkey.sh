#!/usr/bin/bash

sudo updatedb
path=$(locate input-event-codes | awk 'NR==1{print $1}')
echo "Using: $path"

awk -v pattern="KEY_" '$2 ~ pattern {print $2,$3," -> ",strtonum( $3 );}' $path
