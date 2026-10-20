// Culling FRONT_AND_BACK and primitives without faces (nvn_obj.h nvn_prim_has_face, plan item 120; the SDK: only polygons have a face): points,
// lines, line loops and strips (with adjacency too) are drawn under any cull face; triangles, strips, fans, quads, quad strips, polygons,
// triangles with adjacency and patches are culled. Every primitive culled (-DDM_TEST_CULL_ALL_PRIMS, as before): 6 fail.
#include "../nvn/nvn_obj.h"
#include <stdio.h>
static int n, fail;
static void check(const char *name, int want, int got) { n++; fail += want != got; printf("%-44s %s %s\n", name, got ? "culled" : "drawn ", want == got ? "✓" : "✗"); }
int main(void) {
    static const struct { const char *name; int prim, faced; } c[] = {
        {"POINTS", NVN_DRAW_PRIMITIVE_POINTS, 0}, {"LINES", NVN_DRAW_PRIMITIVE_LINES, 0}, {"LINE_LOOP", NVN_DRAW_PRIMITIVE_LINE_LOOP, 0},
        {"LINE_STRIP", NVN_DRAW_PRIMITIVE_LINE_STRIP, 0}, {"LINES_ADJACENCY", NVN_DRAW_PRIMITIVE_LINES_ADJACENCY, 0},
        {"LINE_STRIP_ADJACENCY", NVN_DRAW_PRIMITIVE_LINE_STRIP_ADJACENCY, 0},
        {"TRIANGLES", NVN_DRAW_PRIMITIVE_TRIANGLES, 1}, {"TRIANGLE_STRIP", NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP, 1}, {"TRIANGLE_FAN", NVN_DRAW_PRIMITIVE_TRIANGLE_FAN, 1},
        {"QUADS", NVN_DRAW_PRIMITIVE_QUADS, 1}, {"QUAD_STRIP", NVN_DRAW_PRIMITIVE_QUAD_STRIP, 1}, {"POLYGON", NVN_DRAW_PRIMITIVE_POLYGON, 1},
        {"TRIANGLES_ADJACENCY", NVN_DRAW_PRIMITIVE_TRIANGLES_ADJACENCY, 1}, {"TRIANGLE_STRIP_ADJACENCY", NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP_ADJACENCY, 1},
        {"PATCHES", NVN_DRAW_PRIMITIVE_PATCHES, 1}};
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) check(c[i].name, c[i].faced, nvn_prim_has_face(c[i].prim));
    printf("CULL_PRIM %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
}
