// A metallib loaded with its contents copied into memory (nvn_mtl.m lib_from_file, iOS; DM_NVN_LIB_COPY=1 here): the library works and no memory
// region maps the file once it is loaded, so it holds no vnode (MacNativeGames e099353: kept mappings filled an iPad's vnode table). The mapping
// path (the Mac's default, run without the switch: the broken variant for iOS) keeps the file mapped: 1 fails. Needs xcrun metal.
#include "../nvn/nvn_mtl.m"
#include <libproc.h>
#include <mach/mach_vm.h>
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-72s expected %-3lld → got %-3lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
static int mapped(const char *path) {   // memory regions of this process backed by the file
    int n = 0; mach_vm_address_t a = 0; char real[PATH_MAX]; if (!realpath(path, real)) return -1;
    for (;;) {
        mach_vm_size_t sz = 0; vm_region_basic_info_data_64_t info; mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t obj;
        if (mach_vm_region(mach_task_self(), &a, &sz, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &cnt, &obj) != KERN_SUCCESS) break;
        char f[PATH_MAX]; if (proc_regionfilename(getpid(), a, f, sizeof f) > 0 && !strcmp(f, real)) n++;
        a += sz;
    }
    return n;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    const char *tmp = getenv("TMPDIR"); char tdir[PATH_MAX];
    snprintf(tdir, sizeof tdir, "%s/t_libcopy.XXXXXX", tmp ? tmp : "/tmp"); if (!mkdtemp(tdir)) return 2;
    char src[PATH_MAX + 32], air[PATH_MAX + 32], lib[PATH_MAX + 32], cmd[4 * PATH_MAX];
    snprintf(src, sizeof src, "%s/k.metal", tdir); snprintf(air, sizeof air, "%s/k.air", tdir); snprintf(lib, sizeof lib, "%s/k.metallib", tdir);
    FILE *f = fopen(src, "w"); fputs("#include <metal_stdlib>\nkernel void kfn(device float *o [[buffer(0)]]) { o[0] = 1.0f; }\n", f); fclose(f);
    snprintf(cmd, sizeof cmd, "xcrun -sdk macosx metal -c '%s' -o '%s' && xcrun -sdk macosx metallib '%s' -o '%s'", src, air, air, lib);
    if (system(cmd)) { printf("xcrun metal failed\n"); return 2; }
    NSError *e = nil; id<MTLLibrary> l = lib_from_file(@(lib), &e);
    EXPECT("the library loads and has its function", 1, l && [l newFunctionWithName:@"kfn"] != nil);
    EXPECT("no memory region maps the metallib while the library lives", 0, mapped(lib));
    printf("LIBCOPY %s %d/%d\n", fail ? "FAIL" : "OK", count - fail, count);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tdir); system(cmd);
    return fail != 0;
} }
