#!/data/data/com.termux/files/usr/bin/bash -e
cd "${HOME}"
unset LD_PRELOAD
CHROOT="${HOME}/kali-arm64"

if [ ! -f "$CHROOT/root/.version" ]; then
    touch "$CHROOT/root/.version"
fi

user="Kta41"
home="/home/$user"
start=("sudo" "-u" "$user" "/bin/bash" "--login")

if ! grep -q "^${user}:" "$CHROOT/etc/passwd" 2>/dev/null || [[ "$#" != "0" && ("$1" == "-r" || "$1" == "-R") ]]; then
    user="root"
    home="/root"
    start=("/bin/bash" "--login")
    if [[ "$#" != "0" && ("$1" == "-r" || "$1" == "-R") ]]; then
        shift
    fi
fi

mkdir -p "$CHROOT$home"

PROOT_CMD=(
    proot
    --link2symlink
    -0
    -r "$CHROOT"
    -b /dev
    -b /proc
    -b /sys
    -b /sdcard
    -b "$CHROOT$home:/dev/shm"
    -w "$home"
    /usr/bin/env -i
    "HOME=$home"
    "USER=$user"
    "LOGNAME=$user"
    "PATH=/usr/local/sbin:/usr/local/bin:/bin:/usr/bin:/sbin:/usr/sbin:/data/data/com.termux/files/usr/bin"
    "TERM=${TERM:-xterm-256color}"
    "LANG=C.UTF-8"
)

if [ "$#" == "0" ]; then
    exec "${PROOT_CMD[@]}" "${start[@]}"
else
    exec "${PROOT_CMD[@]}" "${start[@]}" -c "$*"
fi
