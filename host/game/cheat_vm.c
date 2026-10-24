// Atmosphère's cheat programs (plan item 150b): the file format the Switch scene, Yuzu and Ryujinx use ("[Name]" sections of 32-bit hex words,
// "{Master}" sections that always run) and its virtual machine, written from Atmosphère's documentation of the opcodes (docs/features/cheats.md:
// the encodings and their fields; facts only, Atmosphère's code is GPL) after MacNativeGames PR #22's split of a game-independent VM from the host's
// glue (MIT, open; written anew). Game-independent: host/game/cheats.c gives the regions' bases, guarded memory access and the held buttons. Checked by
// host/tests/t_cheat_vm.c.
//   Encodings (nibbles from the top of the first word; parentheses: a second value word when T = 8):
//   0TMR00AA AAAAAAAA VVVVVVVV (VVVVVVVV)   store V (T bytes) at region M + register R + A
//   1TMCXrAA AAAAAAAA VVVVVVVV (VVVVVVVV)   if T bytes at region M + A (X = 1: + register r) C V; C 1 >, 2 >=, 3 <, 4 <=, 5 ==, 6 !=
//   2X000000                                 end (X 0) / else (X 1)
//   300R0000 VVVVVVVV / 310R0000              loop R = V times / its end
//   400R0000 VVVVVVVV VVVVVVVV                R = V
//   5TMR00AA AAAAAAAA / 5T0R10AA / 5T0R2SAA / 5TMR3SAA   R = T bytes at region M + A / R + A / S + A / region M + S + A
//   6T0RIor0 VVVVVVVV VVVVVVVV                store V at R (o: + register r), I: R += T
//   7T0RC000 VVVVVVVV                         R = R op V (C 0 add, 1 sub, 2 mul, 3 shl, 4 shr)
//   8kkkkkkk                                  if every key of the mask is held
//   9TCRS0s0 / 9TCRS100 VVVVVVVV (VVVVVVVV)   R = S op s / S op V (C 0 add, 1 sub, 2 mul, 3 shl, 4 shr, 5 and, 6 or, 7 not, 8 xor, 9 move,
//                                             10-13 float add / sub / mul / div: single for T = 4, double for T = 8)
//   ATSRIOxa (aaaaaaaa)                       store S (T bytes) at R (O 1: + register x, 2: + a, 3: region x + R, 4: region x + a); I: R += T
//   C0TcSX..                                  if S c operand (X 0: T bytes at region M + a, 1: at region M + register r, 2: at register R + a,
//                                             3: at register R + register r, 4: value V, 5: register X)
//   C10D0Sx0 / C2x0MMMM                       save / restore (x 0 restore, 1 save, 2 clear saved, 3 clear register) one register / a mask
//   C3000XXx                                  static register XX: 00-7F (the host's) read into x, 80-FF (the program's) written from x
//   C4r00000 kkkkkkkk kkkkkkkk                if the 64-bit key mask is held (r 1: while held; r 0: once until it is released)
//   FF0..... / FF1..... / FFFTIX..            pause / resume the process (not done here: logged), debug log (logged)
// A conditional that fails skips to its else or end at the same depth. Registers, saved registers and loop state start at 0 each run; the 256
// static registers stay, and so does a C4 r 0's latch (one per program). A memory read or write that fails, or an unknown or truncated instruction, stops the program.
#include "cheat_vm.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ishex8(const char *s) { for (int i = 0; i < 8; i++) if (!isxdigit((unsigned char)s[i])) return 0; return !isxdigit((unsigned char)s[8]); }
int cheat_parse(const char *text, Cheat *out, int max, char *err, size_t errn) {
    int n = -1; const char *p = text; int line = 0;
    while (*p) {
        const char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p); line++;
        char buf[512]; size_t k = len < sizeof buf - 1 ? len : sizeof buf - 1; memcpy(buf, p, k); buf[k] = 0;
        char *t = buf; while (isspace((unsigned char)*t)) t++;
        size_t l = strlen(t); while (l && isspace((unsigned char)t[l - 1])) t[--l] = 0;
        if ((*t == '[' && l > 1 && t[l - 1] == ']') || (*t == '{' && l > 1 && t[l - 1] == '}')) {
            if (++n >= max) { snprintf(err, errn, "more than %d cheats", max); return -1; }
            memset(&out[n], 0, sizeof out[n]); out[n].master = *t == '{';
            t[l - 1] = 0; snprintf(out[n].name, sizeof out[n].name, "%s", t + 1);
        } else if (*t) {
            for (char *q = t; *q;) {
                while (isspace((unsigned char)*q)) q++;
                if (!*q) break;
                if (!ishex8(q)) { snprintf(err, errn, "line %d: \"%.20s\" is not a word of 8 hex digits", line, q); return -1; }
                if (n < 0) { snprintf(err, errn, "line %d: instructions before any [name]", line); return -1; }
                if (out[n].n >= CHEAT_MAX_WORDS) { snprintf(err, errn, "%s: more than %d words", out[n].name, CHEAT_MAX_WORDS); return -1; }
                char w[9]; memcpy(w, q, 8); w[8] = 0; out[n].w[out[n].n++] = (uint32_t)strtoul(w, NULL, 16); q += 8;
            }
        }
        p = e ? e + 1 : p + len;
    }
    return n + 1;
}
#define NIB(w, i) (((w) >> (28 - 4 * (i))) & 0xf)
unsigned cheat_insn_len(const uint32_t *w, unsigned n, unsigned i) {
    if (i >= n) return 0;
    uint32_t a = w[i]; unsigned op = NIB(a, 0), t = NIB(a, 1), len = 0;
    switch (op) {
        case 0: case 1: len = t == 8 ? 4 : 3; break;
        case 2: len = 1; break;
        case 3: len = NIB(a, 1) ? 1 : 2; break;
        case 4: case 6: len = 3; break;
        case 5: len = 2; break;
        case 7: len = 2; break;
        case 8: len = 1; break;
        case 9: len = NIB(a, 5) ? (t == 8 ? 3 : 2) : 1; break;
        case 0xA: { unsigned o = NIB(a, 5); len = o == 2 || o == 4 || o == 5 ? 2 : 1; break; }
        case 0xC: {
            unsigned sub = NIB(a, 1);
            if (sub == 0) { unsigned x = NIB(a, 5); len = x == 0 || x == 2 ? 2 : x == 4 ? (NIB(a, 2) == 8 ? 3 : 2) : 1; }
            else if (sub <= 3) len = 1;
            else if (sub == 4) len = 3;
            break; }
        case 0xF:
            if (NIB(a, 1) == 0xF) { unsigned s = NIB(a, 2); if (s == 0 || s == 1) len = 1; else if (s == 0xF) { unsigned x = NIB(a, 5); len = x == 0 || x == 2 ? 2 : 1; } }
            break;
    }
    return i + len <= n ? len : 0;
}
static int width_ok(unsigned t) { return t == 1 || t == 2 || t == 4 || t == 8; }
static uint64_t trunc_w(uint64_t v, unsigned t) { return t == 8 ? v : v & ((1ull << (8 * t)) - 1); }
static int cmp(uint64_t a, uint64_t b, unsigned c) { return c == 1 ? a > b : c == 2 ? a >= b : c == 3 ? a < b : c == 4 ? a <= b : c == 5 ? a == b : c == 6 ? a != b : -1; }
static int opens(uint32_t a) { unsigned op = NIB(a, 0); return op == 1 || op == 8 || (op == 0xC && (NIB(a, 1) == 0 || NIB(a, 1) == 4)); }
// From the instruction after a failed conditional at i, to its else (execution goes on after it) or its end
static unsigned skip_from(const Cheat *c, unsigned i, int stop_at_else) {
    int depth = 0;
    for (unsigned k = i; k < c->n;) {
        unsigned len = cheat_insn_len(c->w, c->n, k); if (!len) return c->n;
        uint32_t a = c->w[k];
        if (opens(a)) {
#ifndef DM_TEST_CHEAT_FLAT_SKIP   // negative control of t_cheat_vm only: the nesting ignored
            depth++;
#endif
        }
        else if (NIB(a, 0) == 2) {
            if (depth == 0 && (NIB(a, 1) == 0 || stop_at_else)) return k + 1;
            if (NIB(a, 1) == 0) depth--;
        }
        k += len;
    }
    return c->n;
}
static int rd(const CheatEnv *e, uint64_t addr, unsigned t, uint64_t *v) { *v = 0; return e->read(e->ctx, addr, v, t); }
static int wr(const CheatEnv *e, uint64_t addr, unsigned t, uint64_t v) { return e->write(e->ctx, addr, &v, t); }
static uint64_t arith(unsigned c, unsigned t, uint64_t l, uint64_t r, int *ok) {
    *ok = 1;
    if (c >= 10 && c <= 13) {
        if (t == 4) { float a, b, x; uint32_t ua = (uint32_t)l, ub = (uint32_t)r; memcpy(&a, &ua, 4); memcpy(&b, &ub, 4);
            x = c == 10 ? a + b : c == 11 ? a - b : c == 12 ? a * b : a / b; uint32_t o; memcpy(&o, &x, 4); return o; }
        if (t == 8) { double a, b, x; memcpy(&a, &l, 8); memcpy(&b, &r, 8); x = c == 10 ? a + b : c == 11 ? a - b : c == 12 ? a * b : a / b; uint64_t o; memcpy(&o, &x, 8); return o; }
        *ok = 0; return 0;
    }
    uint64_t v;
    switch (c) {
        case 0: v = l + r; break; case 1: v = l - r; break; case 2: v = l * r; break;
        case 3: v = r >= 64 ? 0 : l << r; break; case 4: v = r >= 64 ? 0 : l >> r; break;
        case 5: v = l & r; break; case 6: v = l | r; break; case 7: v = ~l; break; case 8: v = l ^ r; break; case 9: v = l; break;
        default: *ok = 0; return 0;
    }
    return trunc_w(v, t);
}
int cheat_run(const Cheat *c, CheatEnv *e, char *err, size_t errn) {
    uint64_t reg[16] = {0}, saved[16] = {0}; unsigned loop_at[16] = {0};
    for (unsigned i = 0; i < c->n;) {
        unsigned len = cheat_insn_len(c->w, c->n, i);
        if (!len) { snprintf(err, errn, "word %u (%08x): unknown or truncated", i, c->w[i]); return 0; }
        const uint32_t *w = c->w + i, a = w[0]; unsigned op = NIB(a, 0), next = i + len;
        switch (op) {
            case 0: case 1: {
                unsigned t = NIB(a, 1), m = NIB(a, 2); uint64_t addr = ((uint64_t)(a & 0xff) << 32) | w[1], v = t == 8 ? ((uint64_t)w[2] << 32) | w[3] : w[2];
                if (!width_ok(t) || m > 3) goto bad;
                if (op == 0) { if (!wr(e, e->base[m] + reg[NIB(a, 3)] + addr, t, v)) goto mem; break; }
                unsigned cc = NIB(a, 3); uint64_t x; if (!rd(e, e->base[m] + (NIB(a, 4) ? reg[NIB(a, 5)] : 0) + addr, t, &x)) goto mem;
                int r = cmp(x, trunc_w(v, t), cc); if (r < 0) goto bad;
                if (!r) next = skip_from(c, next, 1);
                break; }
            case 2: if (NIB(a, 1) == 1) next = skip_from(c, next, 0); break;   // an else reached from its true branch: on to its end
            case 3: {
                unsigned r = NIB(a, 3);
                if (NIB(a, 1) == 0) { reg[r] = w[1]; loop_at[r] = next; }
                else if (--reg[r] != 0 && loop_at[r]) next = loop_at[r];
                break; }
            case 4: reg[NIB(a, 3)] = ((uint64_t)w[1] << 32) | w[2]; break;
            case 5: {
                unsigned t = NIB(a, 1), m = NIB(a, 2), r = NIB(a, 3), kind = NIB(a, 4), s = NIB(a, 5); uint64_t addr = ((uint64_t)(a & 0xff) << 32) | w[1], at, v;
                if (!width_ok(t) || (kind != 1 && kind != 2 && m > 3) || kind > 3) goto bad;
                at = kind == 0 ? e->base[m] + addr : kind == 1 ? reg[r] + addr : kind == 2 ? reg[s] + addr : e->base[m] + reg[s] + addr;
                if (!rd(e, at, t, &v)) goto mem;
                reg[r] = v; break; }
            case 6: {
                unsigned t = NIB(a, 1), r = NIB(a, 3); uint64_t v = ((uint64_t)w[1] << 32) | w[2];
                if (!width_ok(t)) goto bad;
                if (!wr(e, reg[r] + (NIB(a, 5) ? reg[NIB(a, 6)] : 0), t, trunc_w(v, t))) goto mem;
                if (NIB(a, 4)) reg[r] += t;
                break; }
            case 7: {
                unsigned t = NIB(a, 1), r = NIB(a, 3), cc = NIB(a, 4); int ok;
                if (!width_ok(t) || cc > 4) goto bad;
                reg[r] = arith(cc, t, reg[r], w[1], &ok); if (!ok) goto bad;
                break; }
            case 8: { uint64_t m = a & 0x0fffffff; if ((e->keys & m) != m) next = skip_from(c, next, 1); break; }
            case 9: {
                unsigned t = NIB(a, 1), cc = NIB(a, 2), r = NIB(a, 3), s = NIB(a, 4); int ok;
                uint64_t rv = NIB(a, 5) ? (t == 8 ? ((uint64_t)w[1] << 32) | w[2] : w[1]) : reg[NIB(a, 6)];
                if (!width_ok(t)) goto bad;
                reg[r] = arith(cc, t, trunc_w(reg[s], t), trunc_w(rv, t), &ok); if (!ok) goto bad;
                break; }
            case 0xA: {
                unsigned t = NIB(a, 1), s = NIB(a, 2), r = NIB(a, 3), inc = NIB(a, 4), o = NIB(a, 5), x = NIB(a, 6); uint64_t rel = len == 2 ? ((uint64_t)NIB(a, 7) << 32) | w[1] : 0, at;
                if (!width_ok(t) || o > 4 || ((o == 3 || o == 4) && x > 3)) goto bad;
                at = o == 0 ? reg[r] : o == 1 ? reg[r] + reg[x] : o == 2 ? reg[r] + rel : o == 3 ? e->base[x] + reg[r] : e->base[x] + rel;
                if (!wr(e, at, t, trunc_w(reg[s], t))) goto mem;
                if (inc) reg[r] += t;
                break; }
            case 0xC: {
                unsigned sub = NIB(a, 1);
                if (sub == 0) {
                    unsigned t = NIB(a, 2), cc = NIB(a, 3), s = NIB(a, 4), x = NIB(a, 5), p6 = NIB(a, 6), p7 = NIB(a, 7); uint64_t rv;
                    if (!width_ok(t)) goto bad;
                    if (x <= 3) {
                        uint64_t rel = ((uint64_t)p7 << 32) | (len == 2 ? w[1] : 0), at;
                        if ((x == 0 || x == 1) && p6 > 3) goto bad;
                        at = x == 0 ? e->base[p6] + rel : x == 1 ? e->base[p6] + reg[p7] : x == 2 ? reg[p6] + rel : reg[p6] + reg[p7];
                        if (!rd(e, at, t, &rv)) goto mem;
                    } else if (x == 4) rv = t == 8 ? ((uint64_t)w[1] << 32) | w[2] : w[1];
                    else if (x == 5) rv = reg[p6];
                    else goto bad;
                    int r = cmp(trunc_w(reg[s], t), trunc_w(rv, t), cc); if (r < 0) goto bad;
                    if (!r) next = skip_from(c, next, 1);
                } else if (sub == 1 || sub == 2) {
                    unsigned x = sub == 1 ? NIB(a, 6) : NIB(a, 2); uint32_t mask = sub == 1 ? 0 : a & 0xffff;
                    if (x > 3) goto bad;
                    for (unsigned k = 0; k < 16; k++) {
                        unsigned d = sub == 1 ? NIB(a, 3) : k, src = sub == 1 ? NIB(a, 5) : k;
                        if (sub == 2 && !(mask >> k & 1)) continue;
                        if (x == 0) reg[d] = saved[src]; else if (x == 1) saved[d] = reg[src]; else if (x == 2) saved[d] = 0; else reg[d] = 0;
                        if (sub == 1) break;
                    }
                } else if (sub == 3) {
                    unsigned xx = (a >> 4) & 0xff, r = NIB(a, 7);
                    if (xx < 0x80) reg[r] = e->statics[xx]; else e->statics[xx] = reg[r];
                } else if (sub == 4) {
                    uint64_t m = ((uint64_t)w[1] << 32) | w[2]; int held = (e->keys & m) == m;
                    if (NIB(a, 2) == 0) { int fire = held && !(e->c4_latched & 1); e->c4_latched = held; held = fire; }
                    if (!held) next = skip_from(c, next, 1);
                } else goto bad;
                break; }
            case 0xF:
                if (NIB(a, 1) == 0xF && (NIB(a, 2) == 0 || NIB(a, 2) == 1)) { e->paused_asked = 1; break; }   // pause / resume: not done here
                if (NIB(a, 1) == 0xF && NIB(a, 2) == 0xF) break;   // debug log: nothing here
                goto bad;
            default: goto bad;
        }
        i = next;
        continue;
    bad: snprintf(err, errn, "word %u (%08x): not a valid instruction", i, a); return 0;
    mem: snprintf(err, errn, "word %u (%08x): a memory access failed", i, a); return 0;
    }
    return 1;
}
