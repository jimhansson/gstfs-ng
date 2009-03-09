#!/bin/sh

fusermount -u "$2"
./gstfs -f -osrc="$1",src_ext=mp3,dst_ext=ogg,pipeline="filesrc name=\"_source\" ! decodebin ! audioconvert ! vorbisenc quality=0.4 ! vorbisparse ! oggmux! fdsink name=\"_dest\" sync=false" "$2"

