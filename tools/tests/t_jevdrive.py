#!/usr/bin/env python3
"""tools/jevdrive.py without the network: the goals' rules (games/totk/jev.json, games/manosaba/jev.json) choose the right key or wait, including
their guards (no A while Tears of the Kingdom's HUD is partly up: A dives off the cliff; no A on manosaba's title unless New Game is selected);
the loop against a fake host (it answers each `shot` line in the command file with a PNG) and scripted judgments presses A twice, stops after two
frames with the HUD, and writes quit; without a key it exits 2 and prints no key. Each rule's first condition alone (DM_TEST_JEV_NO_GUARD=1,
the broken variant): 4 fail."""
import json, os, subprocess, sys, tempfile, threading, time
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools')); import jevdrive
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<96} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
def ch(choice, p): return {'type': 'choice', 'choice': choice, 'confidence': p, 'probabilities': {choice: p}}
def nl(p): return {'type': 'noul', 'noul': p}

totk = jevdrive.load_goal('totk', 'load_save')['rules']
check('TotK: hearts up: done', jevdrive.decide(totk, {'hud': nl(0.95), 'action': ch('done', 0.99)}) == 'done')
check('TotK: title waiting for A, no HUD: A', jevdrive.decide(totk, {'hud': nl(0.02), 'action': ch('press_a', 0.99)}) == {'key': 'A', 'ms': 200, 'settle': 1.0})
check('TotK: A judged but the HUD partly up (0.7): wait (A would dive off the cliff)', jevdrive.decide(totk, {'hud': nl(0.7), 'action': ch('press_a', 0.9)}) == 'wait')
check('TotK: loading: wait', jevdrive.decide(totk, {'hud': nl(0.01), 'action': ch('wait', 0.97)}) == 'wait')
check('TotK: A judged without confidence (0.48): wait', jevdrive.decide(totk, {'hud': nl(0.01), 'action': ch('press_a', 0.48)}) == 'wait')
mano = jevdrive.load_goal('manosaba', 'first_dialogue')['rules']
check('manosaba: title, Load Game selected (locked): RIGHT', jevdrive.decide(mano, {'screen': ch('title', 0.9), 'selected': ch('load_game', 0.8), 'story_text': nl(0.01)})['key'] == 'RIGHT')
check('manosaba: title, New Game selected: A', jevdrive.decide(mano, {'screen': ch('title', 0.9), 'selected': ch('new_game', 0.8), 'story_text': nl(0.01)})['key'] == 'A')
check('manosaba: title, nothing highlighted yet: RIGHT (shows the selection)', jevdrive.decide(mano, {'screen': ch('title', 0.9), 'selected': ch('none', 0.8), 'story_text': nl(0.01)})['key'] == 'RIGHT')
check('manosaba: loading, nothing highlighted: wait', jevdrive.decide(mano, {'screen': ch('loading', 0.9), 'selected': ch('none', 0.9), 'story_text': nl(0.01)}) == 'wait')
check('manosaba: a story line: done', jevdrive.decide(mano, {'screen': ch('story', 0.9), 'selected': ch('none', 0.9), 'story_text': nl(0.95)}) == 'done')

# the loop: a fake host answers shots; judgments scripted
with tempfile.TemporaryDirectory() as td:
    cmd, shots = os.path.join(td, 'cmd.txt'), os.path.join(td, 'shots'); open(cmd, 'w').close()
    stop = threading.Event()
    def host():
        seen = 0
        while not stop.is_set():
            lines = open(cmd).read().splitlines()
            for l in lines[seen:]:
                if l.startswith('shot '): open(l[5:], 'wb').write(b'\x89PNG\r\n\x1a\n' + b'\0' * 8 + b'\0\0\0\0IEND\xaeB`\x82')
            seen = len(lines); time.sleep(0.01)
    th = threading.Thread(target=host, daemon=True); th.start()
    script = [{'hud': nl(0.02), 'action': ch('wait', 0.97)}, {'hud': nl(0.01), 'action': ch('press_a', 0.99)}, {'hud': nl(0.02), 'action': ch('press_a', 1.0)},
              {'hud': nl(0.6), 'action': ch('press_a', 0.9)}, {'hud': nl(0.95), 'action': ch('done', 0.99)}, {'hud': nl(0.95), 'action': ch('done', 0.98)}]
    it = iter(script)
    judge = lambda png: (next(it), 1.0, {'input_tokens': 10})
    rc = jevdrive.main(['--game', 'totk', '--goal', 'load_save', '--cmd', cmd, '--shots', shots, '--limit', '20', '--quit'], judge=judge, sleep=lambda s: time.sleep(min(s, 0.02)))
    stop.set(); th.join(1)
    lines = open(cmd).read().splitlines()
    keys = [l for l in lines if l.startswith('key ')]
    check('loop: reached (exit 0) after two frames with the HUD', rc == 0, f'rc={rc}')
    check('loop: A pressed exactly twice (not with the HUD at 0.6)', keys == ['key A 200', 'key A 200'], str(keys))
    check('loop: six frames judged, then quit', sum(l.startswith('shot ') for l in lines) == 6 and lines[-1] == 'quit', str(lines[-3:]))

# no key: exit 2, nothing secret printed
with tempfile.TemporaryDirectory() as td:
    env = {k: v for k, v in os.environ.items() if k != 'TYPESAFE_API_KEY'}; env['HOME'] = td
    open(os.path.join(td, 'cmd.txt'), 'w').close()
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'jevdrive.py'), '--game', 'totk', '--goal', 'load_save',
                        '--cmd', os.path.join(td, 'cmd.txt'), '--shots', os.path.join(td, 's'), '--limit', '1'], capture_output=True, text=True, env=env)
    check('no key: exit 2 with a message naming where the key goes', r.returncode == 2 and '~/.config/typesafe/api_key' in r.stdout, r.stdout + r.stderr)
    check('no key: no "apikey_" text in the output', 'apikey_' not in r.stdout + r.stderr)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
