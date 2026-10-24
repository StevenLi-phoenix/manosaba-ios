// gfx/nvn/hle/programs.inc, part of hle_nvn.c (included there in order, one translation unit): Programs
// ---------- Programs ----------
NVN(nvnProgramInitialize, NVNboolean, NVNprogram *p, NVNdevice *d) {
    (void)d; memset(p, 0, sizeof(prog_t)); ((prog_t *)p)->magic = MAGIC('P', 'R', 'O', 'G'); return 1;
}
NVN(nvnProgramFinalize, void, NVNprogram *p) { x_drain(); mtl_prog_release((prog_t *)p); ((prog_t *)p)->magic = 0; }
// Shader key = the first 8 bytes of sha256(the 0x880-byte control section + the code area), the code area being the length at control +0x704 read
// from data. The replay layer looks libraries up by it (16 hex digits); the offline tools name captures by it (shmap_from_capture.py, dmsh_build.py,
// engines/unity/shader_extract.py). The control section alone does not identify a shader: it holds sizes and resource counts but no hash of the
// code. Tears of the Kingdom showed 488 more code areas than its 2704 control sections in the first minute from boot, among them its bloom
// upsampling shader (sample × c[3][0x60]) behind the same control section as one that adds c[3][0x20]; the build kept whichever was captured
// first, so the bloom added a matrix row of an earlier pass and the title picture turned red. Same definition as nxnative's keys_hash (seedds, MIT).
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
// The control section's own length (plan item 64l; the GPU code versions of bayonetta3-mobile's nvn_control.h, MIT, as data): magic 0x98761234,
// version 1.12 0x838 bytes, 1.14 0x848, 1.15 0x850, 1.16 0x880, the length at +0x20 (both games: every capture 1.16, 0x880). Reading 0x880 of a
// shorter one hashed and dumped whatever followed it (and could run off its mapping); now those bytes are zeros, so keys and .ctl files keep the
// 0x880 bytes the offline tools read, and a 1.16 key is unchanged. Another version: reported once, read as 0x880 as before
static uint32_t ctl_len(const void *control) {
#ifdef DM_TEST_CTL_FIXED   // negative control of t_nvn only: always 0x880, as before
    (void)control; return 0x880;
#endif
    const uint8_t *p = control; uint32_t magic, major, minor, size;
    memcpy(&magic, p, 4); memcpy(&major, p + 4, 4); memcpy(&minor, p + 8, 4); memcpy(&size, p + 0x20, 4);
    if (magic == 0x98761234u && major == 1 && ((minor == 12 && size == 0x838) || (minor == 14 && size == 0x848) || (minor == 15 && size == 0x850))) return size;
    if (!(magic == 0x98761234u && major == 1 && minor == 16 && size == 0x880)) { static _Atomic int said; if (!atomic_exchange(&said, 1)) LOG("[nvn] [red] shader control section version %u.%u, length %#x (magic %#x): not known, read as 0x880 bytes", major, minor, size, magic); }
    return 0x880;
}
static const uint8_t *ctl_880(const void *control, uint8_t buf[0x880]) {   // the control section as 0x880 bytes, zeros past its own length
    uint32_t n = ctl_len(control); if (n >= 0x880) return control;
    memcpy(buf, control, n); memset(buf + n, 0, 0x880 - n); return buf;
}
static void shader_key(const NVNshaderData *sd, uint8_t key[8]) {
    uint32_t len = *(const uint32_t *)((const uint8_t *)sd->control + 0x704);
    uint8_t md[CC_SHA256_DIGEST_LENGTH], cb[0x880]; CC_SHA256_CTX h; CC_SHA256_Init(&h);
    CC_SHA256_Update(&h, ctl_880(sd->control, cb), 0x880);
    if (sd->data && len && len < (1u << 24)) CC_SHA256_Update(&h, (const void *)(uintptr_t)sd->data, len);
    CC_SHA256_Final(md, &h); memcpy(key, md, 8);
}
// The functional key (mtl_prog_fkeys, plan item 169; Touchable's internal/tools/shader_keys.py, MIT, written anew): control +0x700 the constants' start,
// +0x704 the end of the code area, +0x708 its start, +0x714 the stage (0 vertex, 1 pixel, 5 compute), +0x748 a compute shader's workgroup; the hash of
// {stage, constants' start - start, end - constants' start, workgroup x y z} as six u32 and the bytes from start to end. Shaders the game ships twice
// with other metadata in the control section get one functional key (manosaba's capture: 2,740 shaders, 1,299 functional keys)
static void shader_fkey(const NVNshaderData *sd, uint8_t out[8]) {
    memset(out, 0, 8);
    const uint8_t *c = (const uint8_t *)sd->control; if (!c || !sd->data) return;
    uint32_t cst, end, start, stage, local[3] = {0, 0, 0}; memcpy(&cst, c + 0x700, 4); memcpy(&end, c + 0x704, 4); memcpy(&start, c + 0x708, 4); memcpy(&stage, c + 0x714, 4);
    if ((stage != 0 && stage != 1 && stage != 5) || start >= cst || cst > end || end >= (1u << 24)) return;
    if (stage == 5) memcpy(local, c + 0x748, 12);
    uint32_t hdr[6] = {stage, cst - start, end - cst, local[0], local[1], local[2]};
    uint8_t md[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_CTX h; CC_SHA256_Init(&h);
    CC_SHA256_Update(&h, hdr, sizeof hdr); CC_SHA256_Update(&h, (const uint8_t *)(uintptr_t)sd->data + start, end - start);
    CC_SHA256_Final(md, &h); memcpy(out, md, 8);
}
// DM_SHDUMP=directory (sampling for offline translation): for each new shader write <key>.ctl (the 0x880-byte control section) and <key>.data (from data: 0x30 + the length recorded at control +0x704 + 0x100 bytes).
// The .ctl marks a capture as done (a shader with one is skipped), so it is written last and each file whole (to .tmp, then renamed; plan item 125,
// bayonetta3-mobile writes atomically): the .ctl used to come first, and a run ended between the two left a capture without its code that later
// runs skipped and the offline build tripped on. stop_after_data: the unit test's crash between the two files (t_nvn)
static int shdump_file(const char *path, const void *p, size_t n) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    int bad = fwrite(p, 1, n, f) != n; if (fclose(f)) bad = 1;
    if (bad || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}
static void shdump_to(const char *dir, const NVNshaderData *sd, int stop_after_data) {
    uint8_t md[8]; shader_key(sd, md); char key[17]; for (int i = 0; i < 8; i++) snprintf(key + 2 * i, 3, "%02x", md[i]);
    char pc[1200], pd[1200]; snprintf(pc, sizeof pc, "%s/%s.ctl", dir, key); snprintf(pd, sizeof pd, "%s/%s.data", dir, key);
    if (!access(pc, F_OK)) return;
    uint32_t len = *(const uint32_t *)((const uint8_t *)sd->control + 0x704); uint8_t cb[0x880]; const uint8_t *ctl = ctl_880(sd->control, cb);
#ifndef DM_TEST_SHDUMP_CTL_FIRST   // negative control of t_nvn only: the control section first, as before
    if (shdump_file(pd, (const void *)sd->data, 0x30 + (size_t)len + 0x100) || stop_after_data) return;
    shdump_file(pc, ctl, 0x880);
#else
    if (shdump_file(pc, ctl, 0x880) || stop_after_data) return;
    shdump_file(pd, (const void *)sd->data, 0x30 + (size_t)len + 0x100);
#endif
}
static void shdump(const NVNshaderData *sd) {
    static const char *dir; static int init; if (!init) { dir = getenv("DM_SHDUMP"); init = 1; if (dir) mkdir(dir, 0755); }
    if (!dir || !sd->control) return;
    shdump_to(dir, sd, 0);
}
// Shader stage: a graphics code area starts with the 78563412 marker + 0x30 header, then the first SPH word has type bits 0–4 (1 = VTG, 2 = pixel) and shader type bits 10–13 (1 = vertex, 5 = pixel);
// no marker = compute (data is bare instructions). Same test as the offline shmap_from_capture.py / dmsh_build.py. Needed when BindProgram attaches a program per stage bit (see nvn_mtl.m prog_bind)
static uint8_t shader_stage(const NVNshaderData *sd) {
    const uint8_t *d = (const uint8_t *)sd->data; if (!d) return 0xff;
    if (*(const uint32_t *)d != 0x12345678u) return 5;
    uint32_t w0 = *(const uint32_t *)(d + 0x30);
    if ((w0 & 0x1f) == 1 && ((w0 >> 10) & 0xf) == 1) return 0;
    if ((w0 & 0x1f) == 2 && ((w0 >> 10) & 0xf) == 5) return 1;
    static int red; if (!red++) LOG("[red] ProgramSetShaders: SPH first word %#x is not vertex / pixel (tessellation / geometry shaders not supported)", w0);
    return 0xff;
}
NVN(nvnProgramSetShaders, NVNboolean, NVNprogram *p, int n, const NVNshaderData *sd) {
    NVN_BLOCKING();   // loads / builds shader libraries
    prog_t *o = (prog_t *)p;
    if (o->serial) x_drain();   // its old shaders may still be in a queued replay (plan item 61)
    if (n < 0 || n > 6) { LOG("[red] ProgramSetShaders: stage count %d", n); return 0; }
    for (int i = 0; i < n; i++) shdump(&sd[i]);
    mtl_prog_release(o); o->nstage = n; o->serial = nvn_new_serial();
    uint8_t fk[6][8] = {{0}};
    for (int i = 0; i < n; i++) { shader_key(&sd[i], o->key[i]); shader_fkey(&sd[i], fk[i]); o->kstage[i] = shader_stage(&sd[i]); }
    mtl_prog_fkeys(o->serial, fk);
    // DM_SHDUMP also records which vertex and pixel shaders the game puts in one program (<dir>/pairs.tsv, one "vertex key<TAB>pixel key" line per
    // new pair): the offline build pads a vertex shader with the outputs its paired pixel shaders read but it does not write (dmsh_build.py vs_pad;
    // Metal refuses to link such a pair). Duplicates are filtered here as long as the process runs; the build deduplicates the file anyway. The pairs
    // pipelines are built for are written too (nvn_mtl.m pair_note), among them those BindProgram makes from two programs' stages
    { static const char *dir; static int init; static nvn_hmap seen; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
      if (!init) { dir = getenv("DM_SHDUMP"); init = 1; }
      int vs = -1, ps = -1; for (int i = 0; i < n; i++) { if (o->kstage[i] == 0) vs = i; else if (o->kstage[i] == 1) ps = i; }
      if (dir && vs >= 0 && ps >= 0) {
          uint8_t k[16]; memcpy(k, o->key[vs], 8); memcpy(k + 8, o->key[ps], 8);
          pthread_mutex_lock(&mu); if (!seen.klen) nvn_hm_init(&seen, 16);
          if (!nvn_hm_get(&seen, k)) { nvn_hm_put(&seen, k, (void *)1);
              char pth[1200]; snprintf(pth, sizeof pth, "%s/pairs.tsv", dir); FILE *f = fopen(pth, "a");
              if (f) { for (int j = 0; j < 8; j++) fprintf(f, "%02x", o->key[vs][j]); fputc('\t', f); for (int j = 0; j < 8; j++) fprintf(f, "%02x", o->key[ps][j]); fputc('\n', f); fclose(f); } }
          pthread_mutex_unlock(&mu); } }
    OLOG("ProgramSetShaders %p n=%d data0=%#llx", (void *)p, n, n ? (unsigned long long)sd[0].data : 0ull);
    return 1;
}

