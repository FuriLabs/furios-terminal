#!/bin/bash
rsync -avz --progress * droidian@$1:"/home/droidian/furios-terminal/"
ssh droidian@$1 "pkill -f furios-terminal"
ssh droidian@$1 "cd /home/droidian/furios-terminal; meson compile -C build; sudo -S ./build/furios-terminal;"