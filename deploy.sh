#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 [workspaceFolder] [hostname]"
    exit 1
fi

echo "Deploying to $2..."
scp -r $1/src/vma/.libs/libvma.so $1/*.sh $2:$1/
exit $?