// IL2CPP class names (host/il2cpp_obj.h, crash.c crash_objs; plan item 141) on fake objects in a page followed by an unmapped one: an object of a
// class with a namespace reads "UnityEngine.GameObject", one with an empty namespace "<>c"; a name ending 4 bytes before the unmapped page is
// still read; an object on the unmapped page, a small integer and a class whose name is binary give nothing (and no fault). The crash report,
// for a Unity game: registers x0 and x19 named, x5 (the binary name) and x7 (unmapped) not; for another game nothing. Any string taken for a name
// (-DDM_TEST_IL2CPP_NO_CHECK): 2 fail
#include "../il2cpp_obj.h"
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
void crash_objs(const uint64_t x[29]);
static int g_engine;
int dm_engine_il2cpp(void) { return g_engine; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static int rd(uint64_t a, void *d, size_t k) { vm_size_t g = 0; return !vm_read_overwrite(mach_task_self(), (vm_address_t)a, k, (vm_address_t)d, &g) && g == k; }
static uint64_t P(const void *p) { return (uint64_t)(uintptr_t)p; }
static void crash_out(const uint64_t x[29], char *out, size_t cap) {   // crash_objs' stderr
    char path[] = "/tmp/t_il2cpp_obj_XXXXXX"; int fd = mkstemp(path), save = dup(2);
    fflush(stderr); dup2(fd, 2); crash_objs(x); fflush(stderr); dup2(save, 2); close(save);
    ssize_t got = pread(fd, out, cap - 1, 0); out[got > 0 ? got : 0] = 0; close(fd); unlink(path);
}
int main(void) {
    size_t pg = (size_t)getpagesize();
    uint8_t *m = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    munmap(m + pg, pg);
    char *s_go = (char *)m + 0x100, *s_ue = (char *)m + 0x140, *s_c = (char *)m + 0x180, *s_empty = (char *)m + 0x1c0, *s_bin = (char *)m + 0x200, *s_end = (char *)m + pg - 4;
    strcpy(s_go, "GameObject"); strcpy(s_ue, "UnityEngine"); strcpy(s_c, "<>c"); s_empty[0] = 0; memcpy(s_bin, "\x01\xff\x02", 4); memcpy(s_end, "Foo", 4);
    uint64_t *k1 = (uint64_t *)(m + 0x400), *k2 = (uint64_t *)(m + 0x440), *k3 = (uint64_t *)(m + 0x480), *k4 = (uint64_t *)(m + 0x4c0);   // image, gc_desc, name, namespace
    k1[2] = P(s_go); k1[3] = P(s_ue); k2[2] = P(s_c); k2[3] = P(s_empty); k3[2] = P(s_bin); k3[3] = P(s_empty); k4[2] = P(s_end); k4[3] = P(s_empty);
    uint64_t *o1 = (uint64_t *)(m + 0x800), *o2 = (uint64_t *)(m + 0x820), *o3 = (uint64_t *)(m + 0x840), *o4 = (uint64_t *)(m + 0x860);
    o1[0] = P(k1); o2[0] = P(k2); o3[0] = P(k3); o4[0] = P(k4);
    char c[200];
    CHECK(il2cpp_class_of(P(o1), rd, c, sizeof c) && !strcmp(c, "UnityEngine.GameObject"), "a class with a namespace: %s", c);
    CHECK(il2cpp_class_of(P(o2), rd, c, sizeof c) && !strcmp(c, "<>c"), "an empty namespace: %s", c);
    CHECK(il2cpp_class_of(P(o4), rd, c, sizeof c) && !strcmp(c, "Foo"), "a name ending 4 bytes before an unmapped page: %s", c);
    CHECK(!il2cpp_class_of(P(m + pg + 64), rd, c, sizeof c) && !il2cpp_class_of(0x42, rd, c, sizeof c), "an object on the unmapped page, a small integer: nothing");
    CHECK(!il2cpp_class_of(P(o3), rd, c, sizeof c), "a class whose name is binary: nothing");
    uint64_t x[29] = {0}; x[0] = P(o1); x[5] = P(o3); x[7] = P(m + pg + 8); x[19] = P(o2);
    char out[2048];
    g_engine = 1; crash_out(x, out, sizeof out);
    CHECK(strstr(out, "x0 → an IL2CPP UnityEngine.GameObject\n") && strstr(out, "x19 → an IL2CPP <>c\n") && !strstr(out, "x5 ") && !strstr(out, "x7 "),
          "the crash report of a Unity game: x0 and x19 named, x5 and x7 not");
    g_engine = 0; crash_out(x, out, sizeof out);
    CHECK(!out[0], "another game: nothing");
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
