# "Readable-tile" version of a fragment shader (merge B step 2): within one pass, when a later draw samples, at its own pixel, a color attachment written by an earlier draw,
# it is changed to read the color directly from the tile (framebuffer fetch); the two render passes become one, and the intermediate attachment need not be stored to main memory and loaded back just to be sampled.
# Built offline; at runtime only function-constant specialization is done (no source compilation). Usage: variant(msl, bindings, om_only_ok=False) → (new MSL, metadata) or (None, reason)
# Function constant numbering: FX_i (which color index the i-th rewritable sample reads, -1 = sample as usual) = i; FB_k (enable color input k) = 32 + k;
#               OM_j_k (the j-th output writes to attachment k) = 64 + 8 j + k
# Rewritable samples: .sample(sampler, select(_N, float2(0.0), isnan(_N))) on a 2D float texture, where _N = float2(in.m_X.x, in.m_X.y) (interpolated output unchanged);
# whether the texel is really fetched at this pixel is proven at runtime (correspondence between vertex uv and position, texture size = target size); if it cannot be proven, the original version is used.
# Also _N = float2(in.m_X.x * r, in.m_X.y * r) with r = 1.0 / in.m_X.w of a perspective-interpolated input ("proj": the decals of Tears of the Kingdom's
# G-buffer, which read their own pixel of the pass they draw into): at a pixel centre that is exactly the pixel's texel centre when the paired vertex
# shader writes that output as ((w + x) * 0.5, (w - y) * 0.5, -, w) of its final position (screen_locn below, the vertex side of the proof) and the
# viewport covers the whole target; the host checks both. After OJZen/totk-mobile's framebuffer-fetch proof (host/nvn_framebuffer_fetch.inc,
# Apache-2.0), written anew for this tool
import os, re

NFB = 8
_NO_PROJ = bool(os.environ.get('DM_TEST_FETCH_NO_PROJ'))   # negative control of tools/tests/t_dmsh_fetch.py only: projected samples left alone (before 2026-10-22)

def _def(src, v):   # the only definition "float v = ...;" of an SSA value (None when there is not exactly one)
    d = re.findall(r'\n +float %s = ([^;\n]+);' % re.escape(v), src)
    return d[0] if len(d) == 1 else None

def _mul(e):   # the two operands of "_a * _b" / "spvFMul(_a, _b)"
    m = e and (re.fullmatch(r'(_\d+) \* (_\d+)', e) or re.fullmatch(r'spvFMul\((_\d+), (_\d+)\)', e))
    return (m.group(1), m.group(2)) if m else None

def _projected(msl, va, vb):
    """The interpolated input X when va = in.X.x * r and vb = in.X.y * r with r = 1.0 / in.X.w (operands in either order), else None"""
    def part(v):
        p = _mul(_def(msl, v))
        for a, r in ((p, p[::-1]) if p else ()):
            mi = re.fullmatch(r'in\.(m_\d+)\.([xy])', _def(msl, a) or ''); mr = re.fullmatch(r'1\.0 / (_\d+)', _def(msl, r) or '')
            if mi and mr: return mi.group(1), mi.group(2), mr.group(1)
        return None
    pa, pb = part(va), part(vb)
    if not pa or not pb or pa[0] != pb[0] or (pa[1], pb[1]) != ('x', 'y') or pa[2] != pb[2]: return None
    mw = re.fullmatch(r'in\.(m_\d+)\.w', _def(msl, pa[2]) or '')
    return pa[0] if mw and mw.group(1) == pa[0] else None

def screen_locn(vs):
    """Vertex shader: the user(locn) outputs written as ((w + x) * 0.5, (w - y) * 0.5, -, w) of the final position, sorted. Each component used is decided
    by its last write, made outside any block after any whole-vector write, and the function has one return, its last statement. Anything else: not proven"""
    body = re.search(r'^vertex \w+ main0\(.*?\)\n\{\n(.*?)\n\}\n', vs, re.S | re.M)
    ost = re.search(r'struct main0_out\n\{\n(.*?)\n\};', vs, re.S)
    if not body or not ost: return []
    b = body.group(1)
    rets = [m.start() for m in re.finditer(r'^ +return\b', b, re.M)]
    if len(rets) != 1 or b[rets[0]:].rstrip() != '    return out;': return []
    def last(vec, comp):
        if re.search(r'^ +out\.%s\.[xyzw]{2,4} = ' % vec, b, re.M): return None   # (a swizzled write)
        every = [(m.start(), m.group(2)) for m in re.finditer(r'^( +)out\.%s\.%s = ([^;\n]+);$' % (vec, comp), b, re.M) if len(m.group(1)) == 4] + \
                [(m.start(), None) for m in re.finditer(r'^( +)out\.%s\.%s = ' % (vec, comp), b, re.M) if len(m.group(1)) != 4]
        if not every: return None
        p, v = max(every)
        if v is None or not re.fullmatch(r'_\d+', v) or any(m.start() > p for m in re.finditer(r'^ +out\.%s = ' % vec, b, re.M)): return None
        return v
    X, Y, W = last('gl_Position', 'x'), last('gl_Position', 'y'), last('gl_Position', 'w')
    if not (X and Y and W): return []
    def half(v):   # v = s * 0.5 -> s
        e = _def(b, v) or ''
        m = re.fullmatch(r'(_\d+) \* 0\.5', e) or re.fullmatch(r'spvFMul\((_\d+), 0\.5\)', e)
        return m.group(1) if m else None
    def add(v):   # v = a + b -> {a, b}
        e = _def(b, v or '') or ''
        m = re.fullmatch(r'(_\d+) \+ (_\d+)', e) or re.fullmatch(r'spvFAdd\((_\d+), (_\d+)\)', e)
        return {m.group(1), m.group(2)} if m and m.group(1) != m.group(2) else None
    res = []
    for mem, loc in re.findall(r'^    float4 (m_\d+) \[\[user\(locn(\d+)\)\]\];$', ost.group(1), re.M):
        ox, oy, ow = last(mem, 'x'), last(mem, 'y'), last(mem, 'w')
        if not (ox and oy and ow) or ow != W: continue
        sx, sy = add(half(ox)), add(half(oy))
        if sx != {W, X} or not sy or W not in sy: continue
        if _def(b, (sy - {W}).pop()) == '-' + Y: res.append(int(loc))
    return sorted(res)

def variant(msl, bindings, om_only_ok=False):
    tex = {m.group(1) for m in re.finditer(r'texture2d<float> (\w+) \[\[id\(\d+\)\]\];', msl)}
    locn = {m.group(1): int(m.group(2)) for m in re.finditer(r'float4 (m_\d+) \[\[user\(locn(\d+)\)\]\];', msl.split('struct main0_in', 1)[-1].split('};', 1)[0])}
    reg = {b['msl_name']: b['reg'] for b in bindings if b.get('kind') == 'tex' and b.get('msl_name') and b.get('reg') is not None}
    pat = re.compile(r'spvDescriptorSet0\.(\w+)\.sample\(spvDescriptorSet0\.(\w+)Smplr, select\((_\d+), float2\(0\.0\), isnan\(\3\)\)\)')
    calls, out_parts, pos = [], [], 0
    for m in pat.finditer(msl):
        member, var = m.group(1), m.group(3)
        if member not in tex or member != m.group(2) or member not in reg: continue
        d = re.search(r'float2 %s = float2\((_\d+), (_\d+)\);' % var, msl)
        a = d and re.search(r'float %s = in\.(m_\d+)\.x;' % d.group(1), msl); b = d and re.search(r'float %s = in\.(m_\d+)\.y;' % d.group(2), msl)
        proj = None
        if not (a and b) or a.group(1) != b.group(1) or a.group(1) not in locn:
            proj = d and not _NO_PROJ and _projected(msl, d.group(1), d.group(2))
            if not proj or proj not in locn: continue
        i = len(calls)
        if i >= 32: break
        sel = ''.join(f'FX_{i} == {k} ? fb{k} : ' for k in range(NFB))
        out_parts.append(msl[pos:m.start()]); out_parts.append(f'({sel}{m.group(0)})'); pos = m.end()
        calls.append({'fc': i, 'tex_reg': reg[member], 'varying_locn': locn[proj or a.group(1)]} | ({'proj': 1} if proj else {}))
    if not calls and not om_only_ok:
        return None, 'no sample to rewrite'
    # Mixed-use slots: the same texture also has other (non-rewritable) uses - at runtime this texture must not be switched to tile reads (the rewritten reads and the unrewritten ones would not see the same data)
    conv = {}
    for c in calls: conv[c['tex_reg']] = conv.get(c['tex_reg'], 0) + 1
    mixed = sorted({reg[mb] for mb in reg if mb in tex and msl.count(f'spvDescriptorSet0.{mb}.') > conv.get(reg[mb], 0)})
    body = ''.join(out_parts) + msl[pos:]
    om = re.search(r'struct main0_out\s*\{(.*?)\};', body, re.S)
    if not om: return None, 'no output struct'
    outs = [(t, n, int(c)) for t, n, c in re.findall(r'(\w+) (m_\d+) \[\[color\((\d+)\)\]\];', om.group(1))]
    if not outs or len(outs) > 4 or re.sub(r'\w+ m_\d+ \[\[color\(\d+\)\]\];', '', om.group(1)).strip(): return None, 'the output struct is not plain color outputs (or more than 4)'
    members = ''.join(f'    {t} {n}_at{k} [[color({k}), function_constant(OM_{j}_{k})]];\n' for j, (t, n, c) in enumerate(outs) for k in range(NFB))
    body = body[:om.start(1)] + '\n' + members + body[om.end(1):]
    consts = ''.join(f'constant int FX_{c["fc"]} [[function_constant({c["fc"]})]];\n' for c in calls) + \
             ''.join(f'constant bool FB_{k} [[function_constant({32 + k})]];\n' for k in range(NFB)) + \
             ''.join(f'constant bool OM_{j}_{k} [[function_constant({64 + 8 * j + k})]];\n' for j in range(len(outs)) for k in range(NFB))
    body = body.replace('using namespace metal;\n', 'using namespace metal;\n\n' + consts, 1)
    i0 = body.find('fragment main0_out main0(')
    i1 = body.find(')\n{', i0) if i0 >= 0 else -1   # end of signature: closing parenthesis immediately followed by a newline and an opening brace (parameter attributes contain brackets such as [[buffer(0)]])
    if i0 < 0 or i1 < 0: return None, 'main function signature not recognized'
    body = body[:i1] + ''.join(f', float4 fb{k} [[color({k}), function_constant(FB_{k})]]' for k in range(NFB)) + body[i1:]
    decl = ''.join(f'    {t} spv_o_{n} = {t}(0);\n' for t, n, c in outs)
    body = body.replace('    main0_out out = {};\n', '    main0_out out = {};\n' + decl, 1)
    for t, n, c in outs: body = body.replace(f'out.{n}', f'spv_o_{n}')
    put = ''.join(f'    if (OM_{j}_{k}) out.{n}_at{k} = spv_o_{n};\n' for j, (t, n, c) in enumerate(outs) for k in range(NFB))
    if body.count('    return out;\n') < 1: return None, 'no return out'
    body = body.replace('    return out;\n', put + '    return out;\n')
    return body, {'calls': calls, 'mixed_regs': mixed, 'outputs': [{'color': c, 'type': t} for t, n, c in outs]}
