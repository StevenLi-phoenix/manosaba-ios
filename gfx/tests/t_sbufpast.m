// A storage binding whose range runs past the end of its pool (nvn_mtl.m sbuf_gpu_addr, plan item 233, after nxport 79b736d): a compute kernel
// writes two words at the start of storage slot 0, bound 64 bytes before its pool's end with a size of 4096. The words land in the pool, and the
// case is counted; a binding in no pool still gets the dummy buffer and is counted as lost. The whole range required, as before
// (-DDM_TEST_SBUF_WHOLE_RANGE): the dummy buffer, the writes lost, nothing counted: 3 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { unsigned long long w=(unsigned long long)(want), g=(unsigned long long)(got); count++; printf("%-84s expected %-10llx → got %-10llx %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    uint8_t *mem; if (posix_memalign((void **)&mem, getpagesize(), 65536)) abort(); memset(mem, 0, 65536); mtl_pool_add(mem, 65536);
    NSDictionary *j = @{@"route": @"B", @"stage": @"cs", @"c2": @{@"tex": @[@(mtl_c2_tex_base()), @128], @"samp": @[@(mtl_c2_samp_base()), @128]},
        @"bindings": @[@{@"msl_name": @"cp_s0", @"kind": @"sbuf", @"arg_id": @0, @"reg": @0, @"rw": @YES}], @"drv_reads": @{}, @"threadgroup_size": @[@1, @1, @1]};
    nvn_refl rf; char err[256] = ""; nvn_refl_from_json(j, &rf, err, sizeof err);
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nstruct A { device uint *cp_s0 [[id(0)]]; };\n"
        "kernel void w(constant A &a [[buffer(0)]]) { a.cp_s0[0] = 0xabcd1234u; a.cp_s0[1] = 0x5678u; }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    static prog_t pg = {.serial = 8730}; NvProg *p = [NvProg new]; p->sh[5] = [NvShader new]; p->sh[5]->stage = 5; p->sh[5]->fn = [l newFunctionWithName:@"w"];
    p->sh[5]->rf = rf; p->sh[5]->ae = [p->sh[5]->fn newArgumentEncoderWithBufferIndex:0]; serial_put(pg.serial, p);
    chain c = {0}; if (posix_memalign((void **)&c.p, getpagesize(), 65536)) abort(); memset(c.p, 0, 65536); mtl_pool_add(c.p, 65536);
    uint64_t at = (uint64_t)(uintptr_t)mem + 65536 - 64, far = 0x7f0000000000ull;   // 64 bytes before the end; an address in no pool
    p_bind_program bp = {&pg, NVN_SHADER_STAGE_COMPUTE_BIT}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    p_bind_buf sb = {NVN_SHADER_STAGE_COMPUTE, 0, at, 4096}; emit(&c, OP_BIND_SSBO, &sb, sizeof sb);
    p_dispatch d = {1, 1, 1, 0}; emit(&c, OP_DISPATCH, &d, sizeof d);
    p_bind_buf sf = {NVN_SHADER_STAGE_COMPUTE, 0, far, 256}; emit(&c, OP_BIND_SSBO, &sf, sizeof sf);
    emit(&c, OP_DISPATCH, &d, sizeof d); emit(&c, OP_END, NULL, 0);
    uint64_t p0, l0; mtl_sbuf_stats(&p0, &l0);
    state_reset(); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); mtl_sub_snapshot(&hh, 1); mtl_exec(c.p);
    EXPECT("submission completed", 1, mtl_wait(mtl_sub_commit(), 5000000000ull));
    EXPECT("no error in the submission's feedback", 0, mtl_last_commit_error());
    uint64_t p1, l1; mtl_sbuf_stats(&p1, &l1); const uint32_t *o = (const uint32_t *)(uintptr_t)at;
    EXPECT("bound 64 bytes before its pool's end with 4096 bytes: word 0 written [-DDM_TEST_SBUF_WHOLE_RANGE: 0]", 0xabcd1234u, o[0]);
    EXPECT("... word 1 written [-DDM_TEST_SBUF_WHOLE_RANGE: 0]", 0x5678u, o[1]);
    EXPECT("... and counted once as past its pool", 1, p1 - p0);
    EXPECT("a binding in no pool: the dummy buffer, counted as lost", 1, l1 - l0 >= 1);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
