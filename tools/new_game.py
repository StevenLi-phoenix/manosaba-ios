#!/usr/bin/env python3
# Start a new game directory: games/<id>/game.json (profile drafted from the dumped ExeFS and RomFS) and games/<id>/rules.py (empty rewriter rules).
# Usage: new_game.py <id> <ExeFS dir> <RomFS dir> [--title T] [--version V] [--language L]
#   Both dirs must be under DM_ROOT (default ~/dm-work): the profile stores workspace paths relative to it.
# Read from the files: modules (main, sdk, subsdk* present in ExeFS; rtld is the host's job), main's NSO build ID, the program ID (main.npdm ACI0),
# the engine (Unity when RomFS has Data/globalgamemanagers), the RomFS key files with sizes and a tree count. Everything else is a default to review:
# title / version (the id unless given), language en-US, screen 1920x1080, save_sync empty (fill in the save files to keep in iCloud sync).
# new_game.py --check [<id>...]: the keys the files decide (program_id, modules, main_build_id, npdm, engine), worked out again from each profile's
#   own workspace and compared with games/<id>/game.json, with the engine markers in its code modules as facts (plan item 64n; nxnative's
#   profile_guess.py --check, MIT, written anew). A game whose dump is not under DM_ROOT is skipped. Exit 1 on a difference
import json, os, struct, sys

UNITY_KEYS = ('Data/globalgamemanagers', 'Data/globalgamemanagers.assets', 'Data/level0', 'Data/resources.assets', 'Data/sharedassets0.assets',
              'Data/Managed/Metadata/global-metadata.dat', 'Data/StreamingAssets/aa/catalog.bin', 'Data/StreamingAssets/aa/settings.json')

def die(msg): sys.exit(f'new_game: {msg}')

def tally(root):
    n = size = 0
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if not x.startswith('.')]
        for f in files:
            if not f.startswith('.'): n += 1; size += os.path.getsize(os.path.join(d, f))
    return n, size

def program_id(npdm):
    d = open(npdm, 'rb').read()
    if d[:4] != b'META': die(f'{npdm}: not an NPDM')
    aci = struct.unpack_from('<I', d, 0x70)[0]
    if d[aci:aci + 4] != b'ACI0': die(f'{npdm}: no ACI0 section')
    return f'{struct.unpack_from("<Q", d, aci + 0x10)[0]:016X}'

def npdm_fields(npdm):   # META 0x14 SystemResourceSize; ACI0 0x30 / 0x34 kernel capabilities, ThreadInfo = low bits 0111 (switchbrew NPDM)
    d = open(npdm, 'rb').read(); aci = struct.unpack_from('<I', d, 0x70)[0]; ko, ks = struct.unpack_from('<II', d, aci + 0x30); prio = [28, 59]
    for i in range(ks // 4):
        w = struct.unpack_from('<I', d, aci + ko + 4 * i)[0]
        if w & 0xf == 0x7: prio = [(w >> 10) & 63, (w >> 4) & 63]
    if os.environ.get('T_NEW_GAME_PRIO_SWAPPED'): prio = prio[::-1]   # negative control of tools/tests/t_new_game.py only
    return {'system_resource_size': f'{struct.unpack_from("<I", d, 0x14)[0]:#x}', 'thread_priority': prio}

MARKERS = [(b'il2cpp_', 'Unity IL2CPP'), (b'UnityEngine', 'Unity'), (b'sead::', 'Nintendo sead'), (b'agl::', 'Nintendo agl'), (b'Unreal', 'Unreal'),
           (b'CRIWARE', 'CRIWARE'), (b'Wwise', 'Wwise'), (b'FMOD', 'FMOD'), (b'Havok', 'Havok'), (b'nvnBootstrapLoader', 'NVN'), (b'vkCreateInstance', 'Vulkan')]   # (nxnative's list, as data)

def file_keys(exefs, romfs):
    """The profile keys the dumped files decide"""
    mods = ['main'] + [m for m in ['sdk'] + sorted(f for f in os.listdir(exefs) if f.startswith('subsdk')) if os.path.isfile(os.path.join(exefs, m))]
    nso = open(os.path.join(exefs, 'main'), 'rb').read(0x50)
    if nso[:4] != b'NSO0': die(f'{exefs}/main: not an NSO')
    return {'program_id': program_id(os.path.join(exefs, 'main.npdm')), 'modules': mods, 'main_build_id': nso[0x40:0x50].hex(),
            'npdm': npdm_fields(os.path.join(exefs, 'main.npdm')), 'engine': 'unity' if os.path.isfile(os.path.join(romfs, 'Data/globalgamemanagers')) else 'unknown'}

def markers(exefs, mods):   # engine markers in the code modules (needs lz4 for the compressed segments: dm's venv)
    try: sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import symaddr
    except ImportError: return ['(lz4 missing: no markers)']
    seen = []
    for m in mods:
        try: img = symaddr.load(os.path.join(exefs, m))
        except Exception as e: seen.append((f'({m}: {e})', '')); continue
        seen += [(name, m) for pat, name in MARKERS if pat in img and name not in [x for x, _ in seen]]
    return [f'{x} ({m})' if m else x for x, m in seen]

def check(ids):
    repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'); root = os.path.abspath(os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work'))
    bad = 0; ids = ids or sorted(d for d in os.listdir(os.path.join(repo, 'games')) if os.path.isfile(os.path.join(repo, 'games', d, 'game.json')))
    for gid in ids:
        g = json.load(open(os.path.join(repo, 'games', gid, 'game.json'), encoding='utf-8'))
        exefs, romfs = (os.path.join(root, g['workspace'][k]) for k in ('exefs', 'romfs'))
        if not os.path.isfile(os.path.join(exefs, 'main.npdm')): print(f'{gid}: no dump under {root}, skipped'); continue
        got = file_keys(exefs, romfs); diff = [k for k in got if got[k] != g.get(k)]
        for k in diff: print(f'{gid}: {k} {g.get(k)!r} in game.json, {got[k]!r} from the files')
        bad += bool(diff)
        print(f'{gid}: {"differs" if diff else "the keys the files decide agree"}; markers: {", ".join(markers(exefs, got["modules"])) or "none"}')
    return bad

def main():
    a = sys.argv[1:]
    if a[:1] == ['--check']: sys.exit(1 if check(a[1:]) else 0)
    opt = {}
    for k in ('--title', '--version', '--language'):
        if k in a: i = a.index(k); opt[k[2:]] = a[i + 1]; del a[i:i + 2]
    if len(a) != 3: die('usage: new_game.py <id> <ExeFS dir> <RomFS dir> [--title T] [--version V] [--language L]')
    gid, exefs, romfs = a[0], os.path.abspath(a[1]), os.path.abspath(a[2])
    repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    gdir = os.path.join(repo, 'games', gid)
    if os.path.exists(gdir): die(f'games/{gid} already exists')
    root = os.path.abspath(os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work'))
    for p in (exefs, romfs):
        if os.path.commonpath([root, p]) != root: die(f'{p} is not under DM_ROOT ({root})')
    main_nso = os.path.join(exefs, 'main')
    nso = open(main_nso, 'rb').read(0x50)
    if nso[:4] != b'NSO0': die(f'{main_nso}: not an NSO')
    mods = ['main'] + [m for m in ['sdk'] + sorted(f for f in os.listdir(exefs) if f.startswith('subsdk')) if os.path.isfile(os.path.join(exefs, m))]
    unity = os.path.isfile(os.path.join(romfs, 'Data/globalgamemanagers'))
    if unity:
        keys = [k for k in UNITY_KEYS if os.path.isfile(os.path.join(romfs, k))]; tree = 'Data'
    else:   # no engine-specific list: the eight largest files
        allf = [(os.path.getsize(os.path.join(d, f)), os.path.relpath(os.path.join(d, f), romfs)) for d, _, fs in os.walk(romfs) for f in fs if not f.startswith('.')]
        keys = [p for _, p in sorted(allf, reverse=True)[:8]]; tree = '.'
    n, size = tally(os.path.join(romfs, tree))
    g = {
        'id': gid, 'title': opt.get('title', gid), 'version': opt.get('version', gid), 'engine': 'unity' if unity else 'unknown',
        'program_id': program_id(os.path.join(exefs, 'main.npdm')), 'modules': mods, 'main_build_id': nso[0x40:0x50].hex(),
        'language': opt.get('language', 'en-US'), 'screen': [1920, 1080], 'guest_heap_mb': 256, 'npdm': npdm_fields(os.path.join(exefs, 'main.npdm')),
        'depth_mode': 'near_is_zero', 'nvn_c2': {'tex': ['0x20', 32], 'samp': ['0x568', 128], 'img': ['0x120', 8]},
        'romfs': {'files': {k: os.path.getsize(os.path.join(romfs, k)) for k in keys}, 'tree': tree, 'tree_files': n, 'tree_bytes': size},
        'save_sync': [],
        'workspace': {
            'exefs': os.path.relpath(exefs, root), 'romfs': os.path.relpath(romfs, root), 'sysfont': 'data/switch/sysfont',
            'product': f'build/{gid}/product', 'shader_map': f'build/{gid}/shader_map.tsv', 'shader_capture': f'build/{gid}/shader_capture',
            'c1': f'build/{gid}/c1_full.tsv', 'msl_mac': 'build/cache/metal_shader_cache', 'msl_ios': 'build/ios/dmsh_b', 'icon': f'data/{gid}/AppIcon1024.png',
        },
    }
    os.makedirs(gdir)
    # one line per short list, like the hand-written profiles
    s = json.dumps(g, ensure_ascii=False, indent=2)
    for k in ('modules', 'screen', 'save_sync'): s = s.replace(json.dumps(g[k], ensure_ascii=False, indent=2).replace('\n', '\n  '), json.dumps(g[k], ensure_ascii=False))
    open(os.path.join(gdir, 'game.json'), 'w', encoding='utf-8').write(s + '\n')
    open(os.path.join(gdir, 'rules.py'), 'w', encoding='utf-8').write(
        f'# Rewriter rules for {gid} (tools/patch_nso.py --rules games/{gid}/rules.py), keyed by the NSO build ID of each module.\n'
        '# Format: see the "Game rules" section at the top of patch_nso.py.\n\nCUTS = {}\nOBS = {}\nPARAMS = {}\n')
    print(f'games/{gid}/game.json: engine {g["engine"]}, modules {" ".join(mods)}, program {g["program_id"]}, main build {g["main_build_id"]}, '
          f'{len(keys)} key files, {tree}: {n} files / {size} bytes')
    print('review: title, version, language, screen, save_sync')

if __name__ == '__main__':
    main()
