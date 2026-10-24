// gfx/tests/core_test.h: the checks of the tests of gfx/nvn/core (plan item 264): each prints its name, the value it got, the one wanted and ✓ or
// ✗ (check_x: in hexadecimal); n and fail count them for the summary line. The backend they link against: core_fakes.c
#pragma once
#include <stdio.h>
static int n, fail;
static void check(const char *name, long long want, long long got) { n++; int ok = want == got; fail += !ok; printf("%-104s %lld (want %lld) %s\n", name, got, want, ok ? "✓" : "✗"); }
static void check_x(const char *name, long long want, long long got) { n++; int ok = want == got; fail += !ok; printf("%-104s %#llx (want %#llx) %s\n", name, got, want, ok ? "✓" : "✗"); }
