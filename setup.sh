#!/bin/bash

# exit code 3: insufficient number of arguments provided
#           4: non-root executer
#           5: required command not found
#           6: ptr-client compilation failure

if [ -z "$1" ]; then
    echo "Usage: bash $0 IGI_SERVER_IP"
    exit 3
fi

if [ "$(whoami)" != "root" ]; then
    echo "You must be root to run this script"
    exit 4
fi

command_ls=("whoami", "git", "make", "mkdir", "sed", "mv", "apt", "grep")
for cmd in "${command_ls[@]}"; do
    if [ -z "$(command -v $cmd)" ]; then
        echo "$cmd not found"
        exit 5
    fi
done

make
if [ ! -f ptr-client ]; then
    echo "ptr-client not compiled"
    exit 6
fi

parent_path="$(pwd)"

binary_path="$parent_path/ptr-client"
igi_sh_path="$parent_path/igi.sh"

mkdir data
data_path="$parent_path/data"

sed -i "s/PTRCLIENT_PATH/$binary_path/" igi_template.sh
sed -i "s/DUMP_DIR_PATH/$data_path/" igi_template.sh
sed -i "s/TARGET_IP/$1/" igi_template.sh
mv igi_template.sh igi.sh

apt update
apt install -y cron

if [ ! -z "$(grep igi.sh /var/spool/cron/crontabs/root)" ]; then
    echo "Crontab already set up"
    exit 0
fi

echo "*/15 * * * * bash $igi_sh_path" | crontab -