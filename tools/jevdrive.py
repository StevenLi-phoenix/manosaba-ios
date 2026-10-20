#!/usr/bin/env python3
"""Screens judged by TypeSafe's Jev model drive a running host to a goal (plan item 180; the d2r-mobile author's method: which key to press on each
screen). The host runs with DM_CMD=<file> (host/cmdchan.c) and DM_NVN_SHOT_OUT=960x540 (Jev takes 280..1568 px a side); this tool appends
`shot <png>`, asks the goal's questions about the frame, and appends the key its rules choose, until the goal's done condition holds on
done_streak frames in a row (exit 0) or the time runs out (exit 1). Fixed key scripts (DM_KEYS) stay the fallback: without a key the tool says so
and exits 2.

Goals: games/<id>/jev.json {goal name: {"questions": {...}, "rules": [...], "done_streak": n}}. Questions are Jev's System One questions
(docs.typesafe.ai/api.md: choice / noul / score; the screenshot is `screen` in the state) in English. Rules are tried in order; the first whose
conditions all hold acts:
  {"when": [{"q": "hud", "noul_ge": 0.8}], "do": "done"}
  {"when": [{"q": "action", "choice": "press_a", "p_ge": 0.6}, {"q": "hud", "noul_lt": 0.5}], "do": {"key": "A", "settle": 1.0}}
  conditions: noul_ge / noul_lt (a noul's probability), choice (+ optional p_ge: that option's probability); actions: "done", "wait",
  {"key": <DM_KEYS name>, "ms": 200, "settle": <s to wait after>}. No rule: wait.
The key: TYPESAFE_API_KEY or ~/.config/typesafe/api_key (outside the repository; never printed). Screenshots go to TypeSafe's service.
Usage: jevdrive.py --game <id> --goal <name> --cmd <DM_CMD file> --shots <dir> [--limit 120] [--pid <host pid>] [--quit] [--log <jsonl>]
Unit test: tools/tests/t_jevdrive.py (no network)."""
import argparse, base64, json, os, sys, time, urllib.error, urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
API = 'https://api.typesafe.ai/v1/systemone'

def api_key():
    k = os.environ.get('TYPESAFE_API_KEY')
    if k: return k.strip()
    p = os.path.expanduser('~/.config/typesafe/api_key')
    return open(p).read().strip() if os.path.exists(p) else None

def ask(png, questions, key, model='jev-latest', timeout=30):
    """One System One request with the screenshot as `screen` → (answers, ms, usage)"""
    ref = 'screen-' + os.path.basename(png).replace('.', '-')
    body = {'model': model, 'state': {'screen': ref}, 'questions': questions,
            'assets': {ref: {'type': 'image', 'format': 'png', 'data_base64': base64.b64encode(open(png, 'rb').read()).decode()}}}
    req = urllib.request.Request(API, data=json.dumps(body).encode(), method='POST', headers={
        'Authorization': 'Bearer ' + key, 'Content-Type': 'application/json', 'TypeSafe-Danger-Zone-Images': 'true'})
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r: out = json.load(r)
    except urllib.error.HTTPError as e:   # the body names the problem; the request (with the key) is never echoed
        raise RuntimeError(f'Jev HTTP {e.code}: {e.read().decode(errors="replace")[:300]}') from None
    return out.get('answers', {}), (time.monotonic() - t0) * 1000, out.get('usage', {})

def holds(c, answers):
    a = answers.get(c['q'])
    if not a: return False
    if 'noul_ge' in c: return a.get('noul', 0) >= c['noul_ge']
    if 'noul_lt' in c: return a.get('noul', 1) < c['noul_lt']
    if 'choice' in c:
        if a.get('choice') != c['choice']: return False
        return a.get('probabilities', {}).get(c['choice'], 0) >= c.get('p_ge', 0)
    raise ValueError(f'unknown condition {c}')

def decide(rules, answers):
    """The first rule whose conditions all hold → its action ("done", "wait" or {"key": ...}); "wait" when none does"""
    unguarded = os.environ.get('DM_TEST_JEV_NO_GUARD') == '1'   # negative control of t_jevdrive only: each rule's first condition alone
    for r in rules:
        conds = r.get('when', [])[:1] if unguarded else r.get('when', [])
        if all(holds(c, answers) for c in conds): return r['do']
    return 'wait'

def png_complete(p):
    try:
        with open(p, 'rb') as f: f.seek(-12, 2); return f.read().endswith(b'IEND\xaeB`\x82')
    except OSError: return False

def load_goal(game, goal):
    g = json.load(open(os.path.join(ROOT, 'games', game, 'jev.json'), encoding='utf-8'))
    if goal not in g: sys.exit(f'jevdrive: games/{game}/jev.json has no goal {goal} (it has: {", ".join(g)})')
    return g[goal]

def main(argv=None, judge=None, sleep=time.sleep):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', required=True); ap.add_argument('--goal', required=True); ap.add_argument('--cmd', required=True)
    ap.add_argument('--shots', required=True); ap.add_argument('--limit', type=float, default=120); ap.add_argument('--pid', type=int)
    ap.add_argument('--quit', action='store_true'); ap.add_argument('--log')
    a = ap.parse_args(argv)
    goal = load_goal(a.game, a.goal)
    if judge is None:
        key = api_key()
        if not key: print('jevdrive: no TypeSafe key (TYPESAFE_API_KEY or ~/.config/typesafe/api_key): use the fixed keys'); return 2
        judge = lambda png: ask(png, goal['questions'], key)
    os.makedirs(a.shots, exist_ok=True)
    log = open(a.log, 'a') if a.log else None
    def send(line):
        with open(a.cmd, 'a') as f: f.write(line + '\n')
    def alive():
        if not a.pid: return True
        try: os.kill(a.pid, 0); return True
        except OSError: return False
    t0 = time.monotonic(); n = streak = calls = tokens = 0; lat = []; rc = 1
    while time.monotonic() - t0 < a.limit and alive():
        n += 1; shot = os.path.join(a.shots, f'jev{n:04d}.png'); send(f'shot {shot}')
        w0 = time.monotonic()
        while not png_complete(shot) and time.monotonic() - w0 < 8 and alive(): sleep(0.05)
        if not png_complete(shot): continue
        try: answers, ms, usage = judge(shot)
        except Exception as e: print(f'jevdrive: {e}'); sleep(1); continue
        calls += 1; tokens += usage.get('input_tokens', 0); lat.append(ms)
        act = decide(goal['rules'], answers)
        row = {'n': n, 't': round(time.monotonic() - t0, 2), 'ms': round(ms), 'do': act,
               'answers': {q: (v.get('choice'), round(v.get('confidence', v.get('noul', 0)), 2)) for q, v in answers.items()}}
        print(json.dumps(row, ensure_ascii=False), flush=True)
        if log: log.write(json.dumps(row, ensure_ascii=False) + '\n'); log.flush()
        if act == 'done':
            streak += 1
            if streak >= goal.get('done_streak', 2): rc = 0; break
            sleep(0.5); continue
        streak = 0
        if isinstance(act, dict) and 'key' in act:
            send(f'key {act["key"]} {act.get("ms", 200)}'); sleep(act.get('settle', 1.0))
        else: sleep(0.5)
    took = time.monotonic() - t0
    print(json.dumps({'goal': a.goal, 'reached': rc == 0, 'seconds': round(took, 2), 'frames_judged': calls, 'input_tokens': tokens,
                      'latency_ms_p50': round(sorted(lat)[len(lat) // 2]) if lat else None}))
    if a.quit: send('quit')
    return rc

if __name__ == '__main__':
    sys.exit(main())
