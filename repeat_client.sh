#!/bin/bash
printf -v USAGE "Usage: bash $0 TARGET_IP RST_DIR TIME_IN_SEC\n  TARGET_IP:\ttarget ip for ptr-client\n  RST_DIR:\tdirectory to store the result\n  TIME_IN_SEC:\tnumber of seconds to run ptr-client"

if [ "$#" -ne 3 ]; then
    echo "$USAGE"
    exit 3
fi

mkdir "$2"
start=$(date +%s)
maxtime="$3"
while true; do
    cur=$(date +%s)
    if (( start+maxtime < cur )); then
        break
    fi

    ./ptr-client -v "$1" -f "$2/$(date +%s).txt"
done

echo "DONE"

