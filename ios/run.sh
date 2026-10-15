#!/bin/bash
# iOS app: build, install and run on a device. The game is DM_GAME (games/<id>/game.json); its workspace paths (product, shaders, icon, data) are
# relative to DM_ROOT. Device and signing settings come from local.env (see local.env.example).
#   run.sh build              generate Gen/ and the Xcode project, build the app (log: $DM_ROOT/run/ios/build.log)
#   run.sh install            install the built app
#   run.sh launch [seconds]   launch it (ends a running instance), wait, then fetch Documents/boot.txt
#   run.sh log                fetch Documents/boot.txt (or boot.launch.txt) now
#   run.sh push-data          copy the game's RomFS and shared fonts from the workspace into the app (Library/Application Support)
# Build switches: DM_FREE_ACCOUNT=1 (free developer account: no iCloud / memory / sustained-execution entitlements), DM_IOS_M3=1 (older-API build,
# iOS 18), DM_DEVICE_FAMILY=1 (iPhone only).
set -u
cd "$(dirname "$0")"
source tools/local_env.sh
t() { echo "[$(date +%T)] $*"; }
ROOT=${DM_ROOT:-$HOME/dm-work}
GAME=${DM_GAME:-manosaba}; GJ=../games/$GAME/game.json
[ -f "$GJ" ] || { t "[red] no game profile $GJ (DM_GAME=$GAME)"; exit 1; }
get() { python3 ../tools/gen_game.py "$GJ" --get "$1"; }
ws() { local v; v=$(get "workspace.$1") || return 1; echo "$ROOT/$v"; }
R=$ROOT/run/ios; mkdir -p "$R"
DD=${DM_IOS_DD:-$R/dd}
APP=$DD/Build/Products/Release-iphoneos/DmIOS.app
CMD=${1:-}

if [ "$CMD" = build ]; then
  local_env_need DM_TEAM_ID DM_BUNDLE_ID || exit 1
  OUT=$(ws product); MODS=$(get modules); TITLE=$(get title)
  rm -rf Gen; mkdir -p Gen/dm Gen/msl
  # Host sources, header paths and frameworks from host/Makefile; the game's modules as embedded code
  python3 tools/gen_build.py ../host Gen "$GAME" > Gen/dm.xcconfig || { t "[red] gen_build failed"; exit 1; }
  F=$(python3 tools/gen_embed.py "$OUT" Gen/embed.S $MODS) || { t "[red] gen_embed failed ($OUT)"; exit 1; }
  { echo "OTHER_LDFLAGS = \$(inherited) \$(DM_FRAMEWORKS) $F"; echo "DM_GAME_TITLE = $TITLE"; } >> Gen/dm.xcconfig
  for m in $MODS; do cp "$OUT/$m.fixups.tsv" "$OUT/$m.gates.tsv" Gen/dm/ || exit 1; done
  cp "$(ws shader_map)" Gen/dm/shader_map.tsv || { t "[red] no shader stage map"; exit 1; }
  C1=$(ws c1) && [ -f "$C1" ] && cp "$C1" Gen/dm/c1_full.tsv
  # iOS shaders of the current translation chain version
  MSL=$(ws msl_ios); V=$(sed -n 's/^#define DMSH_CHAIN "\(.*\)"/\1/p' ../gfx/nvn/dmsh_chain.h)
  [ -n "$V" ] && ls "$MSL/b-$V" 2>/dev/null | grep -q "_b.metallib$" || { t "[red] no iOS shaders in $MSL/b-$V (gfx/tools/dmsh_build.py with IOSOUT)"; exit 1; }
  cp -cR "$MSL/b-$V" Gen/msl/
  # App icon: the game's icon from the workspace (1024x1024 opaque PNG; game art is never committed); without one the app has no icon
  mkdir -p Gen/AppAssets.xcassets/AppIcon.appiconset; echo '{"info":{"version":1,"author":"xcode"}}' > Gen/AppAssets.xcassets/Contents.json
  ICON=$(ws icon 2>/dev/null)
  if [ -n "$ICON" ] && [ -f "$ICON" ]; then
    cp "$ICON" Gen/AppAssets.xcassets/AppIcon.appiconset/AppIcon1024.png
    echo '{"images":[{"filename":"AppIcon1024.png","idiom":"universal","platform":"ios","size":"1024x1024"}],"info":{"version":1,"author":"xcode"}}' > Gen/AppAssets.xcassets/AppIcon.appiconset/Contents.json
  else
    t "no icon at ${ICON:-(workspace.icon not set)}: building without an app icon"
    echo '{"images":[],"info":{"version":1,"author":"xcode"}}' > Gen/AppAssets.xcassets/AppIcon.appiconset/Contents.json
  fi
  # Variants of the spec go next to project.yml (xcodegen resolves paths relative to the spec) and are removed afterwards
  SPEC=project.yml; FREE=
  if [ "${DM_FREE_ACCOUNT:-0}" = 1 ]; then python3 tools/project_free.py $SPEC project_free.yml || exit 1; SPEC=project_free.yml; FREE=free; fi
  if [ "${DM_IOS_M3:-0}" = 1 ]; then python3 tools/project_m3.py $SPEC project_m3.yml || { rm -f project_free.yml; exit 1; }; SPEC=project_m3.yml; fi
  xcodegen generate -q --spec $SPEC >/dev/null; XG=$?; rm -f project_free.yml project_m3.yml
  [ $XG = 0 ] || { t "[red] xcodegen failed"; exit 1; }
  BN=$(git log -1 --format=%ct 2>/dev/null); [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ] && BN=1; BN=${BN:-1}
  DEST='generic/platform=iOS'
  if [ -n "$FREE" ]; then
    if [ -n "${DM_IOS_UDID:-}" ]; then DEST="id=$DM_IOS_UDID"; else t "free account: set DM_IOS_UDID and connect the phone, or signing has no device to target"; fi
  fi
  t "building $TITLE ($GAME), build number $BN"
  xcodebuild -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination "$DEST" -derivedDataPath "$DD" -allowProvisioningUpdates \
    DM_BUILD_NUMBER=$BN ${DM_DEVICE_FAMILY:+DM_DEVICE_FAMILY=$DM_DEVICE_FAMILY} build > "$R/build.log" 2>&1 || {
      t "[red] build failed (see $R/build.log)"; grep -E 'error:|BUILD FAILED' "$R/build.log" | head -20; exit 1; }
  t "built $APP"; exit 0
fi

local_env_need DM_DEVICE DM_BUNDLE_ID || exit 1
D=$DM_DEVICE; B=$DM_BUNDLE_ID
pull() {   # $1 = file under Documents, $2 = destination
  xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "Documents/$1" --destination "$2" >/dev/null 2>&1
}
case "$CMD" in
install)
  xcrun devicectl device install app --device "$D" "$APP" > "$R/install.log" 2>&1 || { t "[red] install failed"; tail -5 "$R/install.log"; exit 1; }
  t "installed"; exit 0 ;;
launch)
  WAIT=${2:-30}
  xcrun devicectl device process launch --device "$D" --terminate-existing "$B" > "$R/launch.log" 2>&1
  grep -q Launched "$R/launch.log" || { t "[red] launch failed"; tail -8 "$R/launch.log"; exit 1; }
  t "launched, waiting ${WAIT}s"; sleep "$WAIT"
  pull boot.txt "$R/boot.txt" || pull boot.launch.txt "$R/boot.txt" || { t "[red] no log yet"; exit 1; }
  tail -40 "$R/boot.txt"; t "log: $R/boot.txt"; exit 0 ;;
log)
  pull boot.txt "$R/boot.txt" || pull boot.launch.txt "$R/boot.txt" || { t "[red] no log"; exit 1; }
  t "log: $R/boot.txt"; tail -40 "$R/boot.txt"; exit 0 ;;
push-data)
  for x in romfs:romfs sysfont:sysfont; do
    src=$(ws "${x%%:*}") || continue; [ -d "$src" ] || { t "skipped ${x%%:*}: no $src"; continue; }
    src=$(cd "$src" && pwd -P)   # the workspace often links to the data elsewhere; devicectl does not follow a symlinked source directory ("Is a directory")
    t "copying $src → Library/Application Support/${x##*:}"
    xcrun devicectl device copy to --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "$src" \
      --destination "Library/Application Support/${x##*:}" > "$R/push.log" 2>&1 || { t "[red] copy failed"; tail -5 "$R/push.log"; exit 1; }
  done
  t "done"; exit 0 ;;
*)
  sed -n '2,11p' "$0"; exit 1 ;;
esac
