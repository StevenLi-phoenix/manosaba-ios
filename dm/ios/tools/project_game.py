#!/usr/bin/env python3
# 按游戏换 App 的门面：显示名与图标（run.sh 在 DM_GAME 不是 d2r 时调）。输入工程描述（project.yml 或已裁过的 project_free.yml 等），写出 project_game.yml：
#   · 主程序的 CFBundleDisplayName 换成该游戏的名字；
#   · 资源目录 Assets.xcassets 换成 Gen/AppAssets.xcassets（run.sh 从仓库那份拷出来、把 AppIcon1024.png 换成本机 $DM_ROOT/data/<游戏>/AppIcon1024.png；
#     游戏美术是玩家自己的，不进仓库）。两处都核对恰好命中一次，对不上就报错不出文件。
# 用法：project_game.py <输入 yml> <输出 yml> <游戏>
# Per-game App face: display name and icon (called by run.sh when DM_GAME is not d2r). Takes a project description (project.yml, or an already trimmed project_free.yml etc.) and writes project_game.yml:
#   · the main app's CFBundleDisplayName becomes the game's name;
#   · the asset catalog Assets.xcassets becomes Gen/AppAssets.xcassets (run.sh copies it from the repository and replaces AppIcon1024.png with the local $DM_ROOT/data/<game>/AppIcon1024.png;
#     game art belongs to the player and is not committed). Both are checked to match exactly once; on a mismatch it reports an error and produces no file.
# Usage: project_game.py <input yml> <output yml> <game>
import re, sys
src, dst, game = sys.argv[1:4]
NAMES = {'manosaba': '魔法少女ノ魔女裁判'}
if game not in NAMES: sys.exit(f'project_game.py：不认识的游戏 {game}')
s = open(src, encoding='utf-8').read()
s, n_name = re.subn(r'^(        CFBundleDisplayName: )D2R Mobile$', lambda m: m.group(1) + NAMES[game], s, flags=re.M)
s, n_asset = re.subn(r'^(      - )Assets\.xcassets\b', r'\1Gen/AppAssets.xcassets', s, flags=re.M)
if (n_name, n_asset) != (1, 1): sys.exit(f'project_game.py：替换数目不对（显示名 {n_name} / 1、资源目录 {n_asset} / 1）——工程描述的写法变了，先改这个脚本')
open(dst, 'w', encoding='utf-8').write(s)
