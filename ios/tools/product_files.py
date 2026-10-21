#!/usr/bin/env python3
"""The files of a product the app carries beside its embedded code (ios/run.sh copies them into Gen/dm/): per module the loader's fixups and
gates (host/loader.c), and the code option list when the product has one (host/options.c dm_options_embedded reads it to know which options the
embedded image holds). Without that list a product60 build ran its options' code with the host taking them as off: the scene size at the host
page +16 was never written and Tears of the Kingdom drew its scene at 8x8, blurred in blocks on the iPhone (plan item 62l).
Usage: product_files.py <product dir> <module...>: the paths, one a line; exit 1 when a module's fixups or gates are missing."""
import os, sys

def files(out, mods, exists=os.path.exists):
    """(paths, problems) for the modules mods of the product directory out"""
    paths, bad = [], []
    for m in mods:
        for kind in ('fixups', 'gates'):
            p = os.path.join(out, f'{m}.{kind}.tsv')
            (paths if exists(p) else bad).append(p)
        p = os.path.join(out, f'{m}.options.tsv')
        if exists(p) and not os.environ.get('T_PRODUCT_FILES_NO_OPTIONS'): paths.append(p)   # (the env: the broken variant of t_product_files.py)
    return paths, bad

if __name__ == '__main__':
    if len(sys.argv) < 3: print(__doc__); sys.exit(2)
    paths, bad = files(sys.argv[1], sys.argv[2:])
    if bad: print('\n'.join(f'[red] missing {p}' for p in bad), file=sys.stderr); sys.exit(1)
    print('\n'.join(paths))
