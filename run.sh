#!/bin/sh
# Starts the live wallpaper window: Chrome (beta) on its own profile, borderless (--app), muted,
# its app id (chrome-...minimo-abyssal_web_wallpaper.html-...) is what the plugin looks for. Detail: MINIMO_ABYSSAL_DETAIL (2 Low, 1 Medium, 0 High).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WEB="${MINIMO_ABYSSAL_WEB:-$HERE/web}"
PROFILE="${MINIMO_ABYSSAL_PROFILE:-$HOME/.local/share/minimo-abyssal/profil}"
mkdir -p "$PROFILE"
exec /opt/google/chrome-beta/google-chrome-beta \
	--user-data-dir="$PROFILE" \
	--app="file://$WEB/wallpaper.html#detail=${MINIMO_ABYSSAL_DETAIL:-2}&dwell=${MINIMO_ABYSSAL_DWELL:-40}&state=${XDG_RUNTIME_DIR:-/tmp}/minimo-abyssal.state" \
	--ozone-platform=wayland \
	--enable-features=Vulkan \
	--mute-audio \
	--allow-file-access-from-files \
	--no-first-run --no-default-browser-check --noerrdialogs --disable-session-crashed-bubble \
	--password-store=basic
