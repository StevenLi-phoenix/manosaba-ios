// Atmosphère's cheat programs and their virtual machine (plan item 150b): host/cheat_vm.c has the format and the encodings.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { CHEAT_MAX = 128, CHEAT_MAX_WORDS = 0x400, CHEAT_NAME = 96 };
typedef struct { char name[CHEAT_NAME]; int master; unsigned n; uint32_t w[CHEAT_MAX_WORDS]; } Cheat;
// The cheats of a file's text into out (at most max): their count, or -1 with err set. A section without words is kept with n = 0
int cheat_parse(const char *text, Cheat *out, int max, char *err, size_t errn);
// The length in words of the instruction at w[i]; 0: unknown or truncated
unsigned cheat_insn_len(const uint32_t *w, unsigned n, unsigned i);
// One program's surroundings: the regions' bases (0 main, 1 heap, 2 alias, 3 ASLR), the held buttons (nn::hid's order, A = bit 0), guarded
// memory access (0: not readable / refused), the static registers (shared by every program, kept between runs), a C4's latch
typedef struct {
    uint64_t base[4], keys, *statics;
    void *ctx;
    int (*read)(void *ctx, uint64_t addr, void *out, unsigned size);
    int (*write)(void *ctx, uint64_t addr, const void *in, unsigned size);
    int c4_latched, paused_asked;
} CheatEnv;
// Runs one program once: 1, or 0 when it stopped (a failed access, an unknown or invalid instruction) with err set
int cheat_run(const Cheat *c, CheatEnv *e, char *err, size_t errn);
