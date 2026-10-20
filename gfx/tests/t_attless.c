// The size of a render pass without attachments (nvn_obj.h nvn_attless_size, plan item 121): a 4x3 viewport without a scissor gives a 4x3 pass
// (12 fragments; bayonetta3-mobile's case), a viewport at an offset reaches its far edge, fractional edges round up; with a scissor its extent
// wins (Tears of the Kingdom's 1x1 scissor stays 1x1 under a full-screen viewport); nothing set gives 1x1; a huge viewport is capped at 16384.
// 1x1 without a scissor (-DDM_TEST_ATTLESS_SCISSOR_ONLY, as before): 4 fail.
#include "../nvn/nvn_obj.h"
#include <stdio.h>
static int n, fail;
static void check(const char *name, unsigned ww, unsigned wh, unsigned w, unsigned h) { n++; int ok = w == ww && h == wh; fail += !ok; printf("%-64s %ux%u (want %ux%u) %s\n", name, w, h, ww, wh, ok ? "✓" : "✗"); }
int main(void) {
    unsigned w, h;
    float v1[4] = {0, 0, 4, 3}; nvn_attless_size(0, 0, 0, 0, 0, v1, &w, &h); check("a 4x3 viewport, no scissor: 4x3 (12 fragments)", 4, 3, w, h);
    float v2[4] = {10, 20, 30, 40}; nvn_attless_size(0, 0, 0, 0, 0, v2, &w, &h); check("a viewport at 10,20 of 30x40: its far edge, 40x60", 40, 60, w, h);
    float v3[4] = {0.5f, 0, 2.25f, 1.5f}; nvn_attless_size(0, 0, 0, 0, 0, v3, &w, &h); check("fractional edges round up: 3x2", 3, 2, w, h);
    float v4[4] = {0, 0, 1600, 900}; nvn_attless_size(1, 0, 0, 1, 1, v4, &w, &h); check("a 1x1 scissor under a full-screen viewport: 1x1", 1, 1, w, h);
    nvn_attless_size(1, 8, 8, 16, 4, v4, &w, &h); check("a scissor at 8,8 of 16x4: 24x12", 24, 12, w, h);
    nvn_attless_size(0, 0, 0, 0, 0, NULL, &w, &h); check("neither set: 1x1", 1, 1, w, h);
    float v5[4] = {0, 0, 100000, 20}; nvn_attless_size(0, 0, 0, 0, 0, v5, &w, &h); check("a 100000-wide viewport: capped at 16384", 16384, 20, w, h);
    printf("ATTLESS %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
}
