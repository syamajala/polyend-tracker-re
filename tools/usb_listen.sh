#!/bin/sh
# Hear the Tracker's USB audio (its master bus) on this Linux computer's
# speakers: links the OpenTracker capture ports straight to an output with
# PipeWire. A direct link, not a loopback stream, so tools that grab every
# recording stream (Easy Effects) leave it alone.
#
#   tools/usb_listen.sh            link to the default output
#   tools/usb_listen.sh SINK       link to that output (pactl list short sinks)
#   tools/usb_listen.sh off        remove the links
#
# The links go away when the Tracker reconnects (e.g. after a flash): run it again.
set -eu

ports() {   # ports DIR PATTERN: the IDs of matching ports, in FL, FR order
    pw-link "$1" -I | grep -E "$2" | sort -k2 | awk '{print $1}'
}

src=$(ports -o 'alsa_input\.usb-OpenTracker[^ ]*:capture_F[LR]$')
if [ -z "$src" ]; then
    echo "no OpenTracker capture ports: is the Tracker connected?" >&2
    exit 1
fi

if [ "${1:-}" = off ]; then
    # Only links into output devices (other apps, e.g. a level meter, keep theirs).
    outs=" $(ports -i '^ *[0-9]+ alsa_output\.[^ ]*:playback_' | tr '\n' ' ')"
    for s in $src; do
        pw-link -l -I | awk -v s="$s" '
            $1 == s { on = 1; next }
            on && $2 == "|->" { print s, $3; next }
            { on = 0 }' |
        while read -r a b; do
            case "$outs" in *" $b "*) pw-link -d "$a" "$b" && echo "unlinked $a -> $b" ;; esac
        done
    done
    exit 0
fi

sink=${1:-$(pactl get-default-sink)}
case "$sink" in
    *OpenTracker*)
        echo "'$sink' is the Tracker itself (the default output?): name the speakers (pactl list short sinks)" >&2
        exit 1 ;;
esac
dst=$(ports -i "^ *[0-9]+ $(printf '%s' "$sink" | sed 's/[.[\*^$]/\\&/g'):playback_F[LR]$")
if [ -z "$dst" ]; then
    echo "no playback ports for '$sink' (pactl list short sinks)" >&2
    exit 1
fi

set -- $dst
for s in $src; do
    pw-link "$s" "$1" 2>/dev/null && echo "linked $s -> $1" || echo "already linked $s -> $1"
    shift
done
echo "Tracker -> $sink"
