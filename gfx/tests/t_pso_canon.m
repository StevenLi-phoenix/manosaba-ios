// Pipeline keys without the blend state of slots whose blending is off (nvn_pso_skey.h pso_key_blend_canon, plan item 115): two keys that differ
// only in a slot with blending off become the same; a slot with blending on keeps its state, so keys that differ there stay apart; a second pass
// changes nothing; a merged pass's slots follow the same bits. With a player's lists as arguments (pso_user.tsv), the distinct render keys of
// this layout before and after are printed (Tears of the Kingdom's: 729 → 397). Every slot kept (-DDM_TEST_PSO_NO_CANON, as before): 3 fail.
#import <Foundation/Foundation.h>
#include <stdlib.h>
#include "../nvn/nvn_obj.h"
@interface NvShader : NSObject { @public int stage; char key[24]; } @end
@implementation NvShader @end
#include "../nvn/nvn_pso_skey.h"
static int n, fail;
static void check(const char *name, int ok) { n++; fail += !ok; printf("%-80s %s\n", name, ok ? "✓" : "✗"); }
static void key(pso_key *k, unsigned blend_en, unsigned seed) {
    memset(k, 0, sizeof *k); k->cfmt[0] = 70; k->cfmt[1] = 115; k->cfmt[2] = 80; k->blend_en = blend_en;
    for (int i = 0; i < 8; i++) k->blend[i] = (blend_st){.src_rgb = (seed + i) & 0x7f, .dst_rgb = (seed * 3 + i) & 0x7f, .eq_rgb = 1, .src_a = 2, .dst_a = 3, .eq_a = 1};
}
int main(int argc, char **argv) { @autoreleasepool {
    pso_key a, b;
    key(&a, 0x1, 10); key(&b, 0x1, 10); b.blend[1].src_rgb = 9; b.blend[2].dst_a = 7;   // differ only in slots 1 and 2, blending on in slot 0 alone
    pso_key_blend_canon(&a); pso_key_blend_canon(&b);
    check("keys differing only in slots with blending off become one", !memcmp(&a, &b, sizeof a));
    check("... and those slots are zeros", !a.blend[1].src_rgb && !a.blend[2].dst_a && !a.blend[7].eq_rgb);
    key(&a, 0x3, 10); key(&b, 0x3, 10); b.blend[1].src_rgb = 9;
    pso_key_blend_canon(&a); pso_key_blend_canon(&b);
    check("a slot with blending on keeps its state: keys differing there stay apart", memcmp(&a, &b, sizeof a) != 0 && a.blend[1].src_rgb == 11);
    pso_key c = a; pso_key_blend_canon(&c); check("a second pass changes nothing", !memcmp(&a, &c, sizeof a));
    key(&a, 0x4, 20); a.lmap[0] = 2; a.lmap[1] = 0xff; key(&b, 0x4, 20); b.lmap[0] = 2; b.lmap[1] = 0xff; b.blend[0].src_rgb = 1;
    pso_key_blend_canon(&a); pso_key_blend_canon(&b);
    check("merged pass: logical slot 2 blends (kept), slot 0 does not (zeroed)", !memcmp(&a, &b, sizeof a) && a.blend[2].src_rgb == 22);
    for (int f = 1; f < argc; f++) {   // a player's list: distinct keys of this layout before and after
        NSString *txt = [NSString stringWithContentsOfFile:@(argv[f]) encoding:NSUTF8StringEncoding error:nil];
        NSMutableSet *before = [NSMutableSet set], *after = [NSMutableSet set]; int old = 0;
        for (NSString *line in [txt componentsSeparatedByString:@"\n"]) {
            if (![line hasPrefix:@"R\t"]) continue;
            pso_skey sk; if (pso_skey_parse([line substringFromIndex:2].UTF8String, &sk)) { old++; continue; }
            char h[sizeof sk * 2 + 1]; pso_skey_hex(&sk, h); [before addObject:@(h)];
            pso_key_blend_canon(&sk.k); pso_skey_hex(&sk, h); [after addObject:@(h)];
        }
        printf("  %s: %lu render keys of this layout → %lu without the blend state of slots without blending (%d rows of earlier layouts)\n",
               argv[f], (unsigned long)before.count, (unsigned long)after.count, old);
    }
    printf("PSO_CANON %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
} }
