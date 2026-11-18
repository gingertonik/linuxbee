#!/bin/sh
# LOCKED. Written by the reviewer, not the builder.
# Usage: second-disk-trash.sh BUILD-FOLDER | /path/to/linuxbeectl
#
# docs/spec/delete-files.md, FD-TAKEN, on a disk that is not the one of the data folder. There the
# desktop's library sends a file to that disk's own trash (TOP/.Trash/UID or TOP/.Trash-UID), and no
# test that works in a temporary folder can reach one. This does: in a user name space of its own
# (`unshare -r -m`, no root needed) it mounts a memory disk, puts a trash on it, and runs the real
# command. Nothing outside its own temporary folder is touched; the mounts end with the name space.
#
# Where a user name space is refused (most build services) it prints "SKIPPED: ..." and passes: the
# rule is then not held there, and the line says so. The last line is "second-disk-trash: N failed".
set -u
case "${1:-}" in
'') echo "usage: second-disk-trash.sh BUILD-FOLDER | LINUXBEECTL" >&2; exit 2 ;;
/*) ctl=$1 ;;
*) ctl=$(pwd)/$1 ;;
esac
[ -d "$ctl" ] && ctl=$ctl/src/cli/linuxbeectl
[ -x "$ctl" ] || { echo "no linuxbeectl at $ctl" >&2; exit 2; }

if [ "${LINUXBEE_SDT_INSIDE:-}" != 1 ]; then
    if ! command -v unshare > /dev/null 2>&1 || ! unshare -r -m true 2> /dev/null; then
        echo "SKIPPED: no user name spaces here; FD-TAKEN on another disk is not held by this run"
        echo "second-disk-trash: 0 failed"
        exit 0
    fi
    work=$(mktemp -d "${TMPDIR:-/tmp}/lb-sdt.XXXXXX") || exit 2
    LINUXBEE_SDT_INSIDE=1 LINUXBEE_SDT_WORK=$work unshare -r -m sh "$0" "$ctl"
    rc=$?
    rm -rf "$work"
    exit $rc
fi

# ---- from here on: inside the name space, where this user is user 0 ----
S=$LINUXBEE_SDT_WORK
uid=$(id -u)
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
is() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: [$2], wanted [$3]"; fi; }
count() { ls -A "$1" 2> /dev/null | wc -l | tr -d ' '; }
ORPHAN='the file that was in the trash before'
SENT='the file that is sent to the trash'

mkdir -p "$S/try"
if ! mount -t tmpfs -o size=1m tmpfs "$S/try" 2> /dev/null; then
    echo "SKIPPED: a memory disk cannot be mounted in the name space; FD-TAKEN on another disk is not held by this run"
    echo "second-disk-trash: 0 failed"
    exit 0
fi
umount "$S/try"

# fresh NAME KIND: a data folder of its own and a disk of its own with a trash of KIND on it.
#   dash    TOP/.Trash-UID
#   shared  TOP/.Trash/UID, TOP/.Trash with the sticky bit
#   loose   TOP/.Trash without the sticky bit (the library passes it over) and TOP/.Trash-UID
# Sets W, T (the trash the library will use) and SRC (the file to send).
fresh() {
    W=$S/$1
    mkdir -p "$W/home" "$W/data/Trash/files" "$W/data/Trash/info" "$W/config" "$W/cache" "$W/tmp" "$W/disk"
    export HOME="$W/home" XDG_DATA_HOME="$W/data" XDG_CONFIG_HOME="$W/config" XDG_CACHE_HOME="$W/cache" TMPDIR="$W/tmp"
    mount -t tmpfs -o size=8m tmpfs "$W/disk" || { fail "$1: no disk"; return 1; }
    mkdir -p "$W/disk/music"
    case $2 in
    dash) T=$W/disk/.Trash-$uid ;;
    shared) mkdir "$W/disk/.Trash"; chmod 1777 "$W/disk/.Trash"; T=$W/disk/.Trash/$uid ;;
    loose) mkdir -p "$W/disk/.Trash/$uid/files" "$W/disk/.Trash/$uid/info"; chmod 777 "$W/disk/.Trash"; T=$W/disk/.Trash-$uid ;;
    esac
    mkdir -p "$T/files" "$T/info"
    chmod 700 "$T"
    SRC=$W/disk/music/a.mp3
    printf '%s\n' "$SENT" > "$SRC"
}
done_with() { umount "$W/disk" 2> /dev/null; }
send() { "$ctl" trash "$SRC" > "$W/out" 2>&1; rc=$?; }
orphan() { printf '%s\n' "$ORPHAN" > "$T/files/a.mp3"; }
# kept LABEL: the orphan has its bytes, the file went, and went beside it with a record of its own.
kept() {
    is "$1: the command says it went" "$rc $(cut -c1-8 "$W/out" | head -n 1)" "0 trashed:"
    is "$1: the file that was in the trash has its bytes" "$(cat "$T/files/a.mp3" 2> /dev/null)" "$ORPHAN"
    [ ! -e "$SRC" ] && pass "$1: the file left its folder" || fail "$1: the file is still in its folder"
    is "$1: two files in that trash" "$(count "$T/files")" 2
    other=$(ls -A "$T/files" | grep -v '^a\.mp3$' | head -n 1)
    is "$1: the other one is the file that was sent" "$(cat "$T/files/$other" 2> /dev/null)" "$SENT"
    is "$1: one record, and it is the record of that one" "$(ls -A "$T/info")" "$other.trashinfo"
    grep -q '^Path=' "$T/info/$other.trashinfo" 2> /dev/null && pass "$1: the record says where the file was" || fail "$1: the record does not say where the file was"
    is "$1: nothing went to the trash of the data folder" "$(count "$W/data/Trash/files")" 0
}

# 0. The control: with nothing in the way, the library does use the disk's own trash. If it did
# not, the cases below would show nothing.
for kind in dash shared; do
    fresh "control-$kind" $kind || continue
    send
    is "control, $kind: the file went to its own disk's trash" "$rc $(cat "$T/files/a.mp3" 2> /dev/null)" "0 $SENT"
    is "control, $kind: with one record" "$(ls -A "$T/info")" "a.mp3.trashinfo"
    is "control, $kind: and not to the trash of the data folder" "$(count "$W/data/Trash/files")" 0
    done_with
done

# 1. FD-TAKEN on each trash of another disk: a name with no record is not written over.
for kind in dash shared loose; do
    fresh "taken-$kind" $kind || continue
    orphan
    send
    kept "a name with no record, $kind"
    done_with
done

# 2. The same folder reached through a second mount of it: the library goes by the mount.
W=$S/bind
mkdir -p "$W/home" "$W/data/Trash/files" "$W/data/Trash/info" "$W/config" "$W/cache" "$W/tmp" "$W/real/music" "$W/disk"
export HOME="$W/home" XDG_DATA_HOME="$W/data" XDG_CONFIG_HOME="$W/config" XDG_CACHE_HOME="$W/cache" TMPDIR="$W/tmp"
if mount --bind "$W/real" "$W/disk" 2> /dev/null; then
    T=$W/disk/.Trash-$uid
    mkdir -p "$T/files" "$T/info"
    chmod 700 "$T"
    SRC=$W/disk/music/a.mp3
    printf '%s\n' "$SENT" > "$SRC"
    orphan
    printf '%s\n' "$ORPHAN" > "$W/data/Trash/files/a.mp3"
    send
    is "a second mount: the command says it went" "$rc $(cut -c1-8 "$W/out" | head -n 1)" "0 trashed:"
    is "a second mount: the file in that mount's trash has its bytes" "$(cat "$T/files/a.mp3")" "$ORPHAN"
    is "a second mount: the file in the data folder's trash has its bytes" "$(cat "$W/data/Trash/files/a.mp3")" "$ORPHAN"
    umount "$W/disk"
else
    echo "SKIPPED: a second mount of a folder (mount --bind is refused here)"
fi

# 3. A record that another program made is not linuxBee's to remove.
for kind in dash shared; do
    fresh "empty-$kind" $kind || continue
    orphan
    : > "$T/info/a.mp3.trashinfo" # empty: another program has just taken the name
    before=$(ls -i "$T/info/a.mp3.trashinfo" | cut -d' ' -f1)
    send
    is "an empty record of another program, $kind: still there" "$(ls -i "$T/info/a.mp3.trashinfo" 2> /dev/null | cut -d' ' -f1)" "$before"
    is "an empty record of another program, $kind: the file under the name has its bytes" "$(cat "$T/files/a.mp3")" "$ORPHAN"
    done_with
done

# 4. The same, when the other program comes in the middle of linuxBee's work. The moment is made
# with the debugger: the command is stopped at one system call, the other program's step is done by
# hand, and the command goes on. Needs gdb and x86-64 (the first probe reads a register).
if command -v gdb > /dev/null 2>&1 && [ "$(uname -m)" = x86_64 ]; then
    export SHELL=/bin/sh
    # stopped WHERE CONDITION DO: runs the command under gdb, stops at the system call, runs DO there.
    stopped() {
        if [ -n "$2" ]; then
            gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex "catch syscall $1" -ex "condition 1 $2" -ex run \
                -ex "shell $3; : > $W/stopped" -ex delete -ex continue --args "$ctl" trash "$SRC" > "$W/gdb.out" 2>&1
        else
            gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex "catch syscall $1" -ex run \
                -ex "shell $3; : > $W/stopped" -ex delete -ex continue --args "$ctl" trash "$SRC" > "$W/gdb.out" 2>&1
        fi
        [ -e "$W/stopped" ]
    }
    RECORD='[Trash Info]\nPath=/elsewhere/a.mp3\nDeletionDate=2026-01-01T00:00:00\n'

    # Between linuxBee's look and its making of the record, another program makes it.
    fresh "between" dash && {
        orphan
        if stopped openat '$_streq((char*)$rsi, "a.mp3.trashinfo")' ": > $T/info/a.mp3.trashinfo; ls -i $T/info/a.mp3.trashinfo | cut -d' ' -f1 > $W/theirs"; then
            is "a record made between the look and the making: still there, and theirs" "$(ls -i "$T/info/a.mp3.trashinfo" 2> /dev/null | cut -d' ' -f1)" "$(cat "$W/theirs")"
            is "a record made between the look and the making: the file under the name has its bytes" "$(cat "$T/files/a.mp3")" "$ORPHAN"
        else
            echo "SKIPPED: a record made between the look and the making (the command did not stop where this probe waits)"
        fi
        done_with
    }
    # While the library works, another program puts its own record where linuxBee's empty one was.
    fresh "replaced" dash && {
        orphan
        if stopped 'renameat renameat2' '' "rm -f $T/info/a.mp3.trashinfo; printf '$RECORD' > $T/info/a.mp3.trashinfo"; then
            grep -q '^Path=/elsewhere/a.mp3$' "$T/info/a.mp3.trashinfo" 2> /dev/null && pass "a record put in the place of the empty one: still there" || fail "a record put in the place of the empty one: removed"
        else
            echo "SKIPPED: a record put in the place of the empty one (the library did not stop where this probe waits)"
        fi
        done_with
    }
    # While the library works, another program writes its record into the empty one.
    fresh "written" dash && {
        orphan
        if stopped 'renameat renameat2' '' "printf '$RECORD' >> $T/info/a.mp3.trashinfo"; then
            grep -q '^Path=/elsewhere/a.mp3$' "$T/info/a.mp3.trashinfo" 2> /dev/null && pass "a record written into the empty one: still there" || fail "a record written into the empty one: removed"
        else
            echo "SKIPPED: a record written into the empty one (the library did not stop where this probe waits)"
        fi
        done_with
    }
else
    echo "SKIPPED: the three probes that need the debugger (no gdb, or not x86-64)"
fi

echo "second-disk-trash: $fails failed"
[ "$fails" = 0 ]
