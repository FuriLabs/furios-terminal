#!/bin/bash
ssh-copy-id $1@$2
rsync -avz --progress * $1@$2:"/home/$1/furios-terminal/"
ssh $1@$2 "pkill -f furios-terminal"
ssh $1@$2 "cd /home/$1/furios-terminal; meson compile -C build; sudo -S ./build/furios-terminal;"
