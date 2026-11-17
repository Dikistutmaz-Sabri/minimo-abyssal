#!/bin/sh
# minimo-abyssal: install the live wallpaper into Minimo OS's session (or take it out with --remove).
#
#   ./install.sh   build, then:
#     1. ~/.local/lib/minimo-abyssal/minimo-abyssal.so        the plugin (new)
#     2. ~/.local/share/minimo-abyssal/                       run.sh, web/ (the wrapper), vendor/abyssal-garden/
#                                                             (index.html + LICENSE, unchanged); Chrome's own
#                                                             profile goes to profil/ there on first start
#     3. ~/.config/systemd/user/minimo-abyssal.service        starts it with Minimo's session (new), enabled by a
#                                                             link in ~/.config/systemd/user/minimo-session.target.wants/
#     4. ~/.config/hypr/minimo-oturum.lua                     + one line at the end, loading the plugin:
#          pcall(hl.plugin.load, os.getenv("HOME") .. "/.local/lib/minimo-abyssal/minimo-abyssal.so") -- minimo-abyssal
#        (a link into ~/Proyectos/minimo-ayarlar: the real file there gets the line; it is copied to
#        ./backup/<time>/ first). minimo-genie's and minimo-desks' lines stay.
#   ./install.sh --remove   undoes 1-4 (stops the wallpaper if it runs).
# No sudo, no packages. The user's Chrome profile is never used.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SO_DIR="$HOME/.local/lib/minimo-abyssal"
SHARE="$HOME/.local/share/minimo-abyssal"
UNITS="$HOME/.config/systemd/user"
SESSION=$(readlink -f "$HOME/.config/hypr/minimo-oturum.lua")
MARK="-- minimo-abyssal"
LINE="pcall(hl.plugin.load, os.getenv(\"HOME\") .. \"/.local/lib/minimo-abyssal/minimo-abyssal.so\") $MARK"
# systemctl only for the real home (a test with HOME elsewhere must not touch the user's services)
REAL=$([ "$HOME" = "$(getent passwd "$(id -un)" | cut -d: -f6)" ] && echo 1 || echo 0)

[ -f "$SESSION" ] || { echo "missing: $SESSION" >&2; exit 1; }

backup() {
	B="$HERE/backup/$(date +%Y%m%d-%H%M%S)"
	mkdir -p "$B"
	cp -p "$SESSION" "$B/"
	echo "backup: $B"
}

if [ "${1:-}" = "--remove" ]; then
	backup
	[ "$REAL" = 1 ] && systemctl --user stop minimo-abyssal.service 2>/dev/null || true
	rm -f "$UNITS/minimo-session.target.wants/minimo-abyssal.service" "$UNITS/minimo-abyssal.service"
	[ "$REAL" = 1 ] && systemctl --user daemon-reload || true
	sed -i "/$MARK\$/d" "$SESSION"
	[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ] && hyprctl plugin unload "$SO_DIR/minimo-abyssal.so" >/dev/null 2>&1 || true
	rm -rf "$SO_DIR" "$SHARE"
	rm -f "${XDG_RUNTIME_DIR:-/tmp}/minimo-abyssal.state"
	echo "removed. The static wallpaper is back; log out and in to drop the plugin."
	exit 0
fi

make -C "$HERE" minimo-abyssal.so
backup

mkdir -p "$SO_DIR" "$SHARE/web" "$SHARE/vendor/abyssal-garden" "$UNITS/minimo-session.target.wants"
# a new file renamed over the old one: a running Hyprland keeps the old one it has loaded
cp "$HERE/minimo-abyssal.so" "$SO_DIR/.minimo-abyssal.so.new"
mv -f "$SO_DIR/.minimo-abyssal.so.new" "$SO_DIR/minimo-abyssal.so"
cp "$HERE/run.sh" "$SHARE/"
cp "$HERE/web/wallpaper.html" "$HERE/web/wallpaper.js" "$SHARE/web/"
cp "$HERE/vendor/abyssal-garden/index.html" "$HERE/vendor/abyssal-garden/LICENSE" "$SHARE/vendor/abyssal-garden/"
cp "$HERE/systemd/minimo-abyssal.service" "$UNITS/"
ln -sfn "$UNITS/minimo-abyssal.service" "$UNITS/minimo-session.target.wants/minimo-abyssal.service"
[ "$REAL" = 1 ] && systemctl --user daemon-reload || true

if ! grep -q -e "$MARK\$" "$SESSION"; then
	[ -z "$(tail -c1 "$SESSION")" ] || echo >>"$SESSION"
	echo "$LINE" >>"$SESSION"
fi

echo "installed. Log out and into «Minimo OS»."
echo "Off for now: systemctl --user stop minimo-abyssal   Pause: hyprctl abyssal pause|resume"
echo "Detail: Environment=MINIMO_ABYSSAL_DETAIL=1 (Medium) in the service; default 2 (Low)."
echo "After every Hyprland update: ./install.sh again (it rebuilds)."
