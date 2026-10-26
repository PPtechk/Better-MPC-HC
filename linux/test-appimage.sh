#!/bin/bash
# Smoke test of the AppImage on a virtual display. Plays a test video with "Intro", "Episode" and
# "Credits" chapters and saves screenshots: the Skip button, the volume above 100% and the skip itself.
#
# Usage: linux/test-appimage.sh <AppImage> <output folder>
# Needs: Xvfb, xdotool, ImageMagick, ffmpeg, PulseAudio.
set -euo pipefail

APPIMAGE=$(readlink -f "$1")
OUT=$(mkdir -p "$2" && readlink -f "$2")
chmod +x "$APPIMAGE"

# a fresh environment, like a first start on a new machine
export XDG_DATA_HOME="$OUT/data"
export APPIMAGE_EXTRACT_AND_RUN=1
export DISPLAY=:99

cat > "$OUT/chapters.txt" <<'EOF'
;FFMETADATA1
[CHAPTER]
TIMEBASE=1/1000
START=0
END=10000
title=Intro
[CHAPTER]
TIMEBASE=1/1000
START=10000
END=25000
title=Episode
[CHAPTER]
TIMEBASE=1/1000
START=25000
END=30000
title=Credits
EOF
ffmpeg -loglevel error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -f lavfi -i sine=frequency=440 \
    -i "$OUT/chapters.txt" -map 0:v -map 1:a -map_metadata 2 -map_chapters 2 -t 30 \
    -c:v libx264 -pix_fmt yuv420p -c:a aac "$OUT/test.mkv"

Xvfb :99 -screen 0 1920x1080x24 >/dev/null 2>&1 &
XVFB_PID=$!
pulseaudio --start --exit-idle-time=-1 >/dev/null 2>&1 || true
sleep 2

shot() {
    import -window root "$OUT/$1.png"
    echo "Screenshot: $1.png"
}

"$APPIMAGE" "$OUT/test.mkv" > "$OUT/player.log" 2>&1 &
PLAYER_PID=$!

# the largest visible window of the player is its main window
find_player_window() {
    local id best="" best_size=0
    for id in $(xdotool search --onlyvisible --classname "better-mpc-hc64.exe" 2>/dev/null); do
        WIDTH=0
        HEIGHT=0
        eval "$(xdotool getwindowgeometry --shell "$id" 2>/dev/null)" || continue
        if [ $((WIDTH * HEIGHT)) -gt "$best_size" ]; then
            best=$id
            best_size=$((WIDTH * HEIGHT))
        fi
    done
    echo "$best"
}

# the first start prepares the Wine environment, wait for the player window
WINDOW=""
for _ in $(seq 1 180); do
    WINDOW=$(find_player_window)
    [ -n "$WINDOW" ] && break
    sleep 1
done
sleep 2
WINDOW=$(find_player_window)
if [ -z "$WINDOW" ]; then
    shot 0-no-window
    echo "The player window didn't appear, visible windows:" >&2
    for id in $(xdotool search --onlyvisible --name '' 2>/dev/null); do
        echo "  $id $(xdotool getwindowname "$id" 2>/dev/null)" >&2
    done
    kill "$PLAYER_PID" "$XVFB_PID" 2>/dev/null || true
    exit 1
fi
echo "Player window: $WINDOW"

# without a window manager the keyboard input goes to the window under the mouse pointer
eval "$(xdotool getwindowgeometry --shell "$WINDOW")"
xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2))

sleep 2
shot 1-intro-skip-button

xdotool key Up Up Up Up
sleep 0.5
shot 2-volume-120

xdotool key Return
sleep 3
shot 3-after-skip

sleep 13
shot 4-credits-skip-button

if [ -f "$XDG_DATA_HOME/applications/better-mpc-hc.desktop" ]; then
    echo "Menu entry: created"
else
    echo "Menu entry: missing" >&2
fi

kill "$PLAYER_PID" 2>/dev/null || true
pkill -f 'Better-MPC-HC64.exe' 2>/dev/null || true
pkill wineserver 2>/dev/null || true
kill "$XVFB_PID" 2>/dev/null || true
