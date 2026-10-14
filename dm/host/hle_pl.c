// nn::pl 系统共享字体：真机由 pl 服务把系统数据里的字体（BFTTF，异或加密的 TTF）解密后放进共享内存，
// 游戏取到的地址 = 共享内存首址 + 服务报的偏移（sdk+0x40f450），直接指向明文 TTF；大小 = TTF 字节数（sdk+0x40f510）。
// Unity（main+0x90ca30）按字体名映射类型，每 100 ms 查一次 GetSharedFontLoadState，直到 1（已载入）再取地址 / 大小交给 FreeType。
// 宿主：从 DM_SYSFONT 目录（缺省 $DM_ROOT/data/switch/sysfont）读玩家自己主机导出的字体文件，按 Ryujinx（MIT）SharedFontManager 的做法
// 组一块共享内存：每种字体 8 字节头（魔数 0x18029a7f、大小 ^ 0x49621806 后字节反序）+ 明文 TTF，偏移返回头之后。
// BFTTF 解密：逐个小端 u32 异或 0x06186249，首字解出须为魔数，第二字（大小）跳过。也接受已解密的 .ttf。
// 字体所在系统数据（Ryujinx ContentManager）：0100000000000811 标准、0814 简中 + 简中扩展、0812 韩文、0813 繁中、0810 任天堂扩展。
// 共享内存大小 0x1100000（switchbrew pl:u）。请求的字体文件不在就中止并报文件名（真机字体恒在，不能让 Unity 无限等）。
// nn::pl system shared fonts: on the real device the pl service decrypts the fonts in system data (BFTTF, XOR-encrypted TTF) into shared memory;
// the address the game gets = shared memory base + the offset reported by the service (sdk+0x40f450), pointing directly at plain TTF; size = TTF bytes (sdk+0x40f510).
// Unity (main+0x90ca30) maps a font name to a type and polls GetSharedFontLoadState every 100 ms until it is 1 (loaded), then hands address / size to FreeType.
// Host: read the font files exported from the player's own console from the DM_SYSFONT directory (default $DM_ROOT/data/switch/sysfont) and, following Ryujinx's (MIT) SharedFontManager,
// build a shared memory block: per font an 8-byte header (magic 0x18029a7f, size ^ 0x49621806 byte-reversed) + plain TTF, with the offset returned past the header.
// BFTTF decryption: XOR each little-endian u32 with 0x06186249; the first word must decrypt to the magic, the second word (size) is skipped. Already decrypted .ttf files are accepted too.
// System data holding the fonts (Ryujinx ContentManager): 0100000000000811 standard, 0814 Simplified Chinese + extended, 0812 Korean, 0813 Traditional Chinese, 0810 Nintendo extended.
// Shared memory size 0x1100000 (switchbrew pl:u). If a requested font file is missing, abort and name the file (the real device always has the fonts; Unity must not wait forever).
#include "hle.h"
#include <sys/stat.h>

#define PL_SHMEM 0x1100000u
enum { NFONT = 6 };
static const struct { const char *file, *title; } g_font[NFONT] = {   // 下标 = SharedFontType | index = SharedFontType
    {"nintendo_udsg-r_std_003", "0100000000000811"},
    {"nintendo_udsg-r_org_zh-cn_003", "0100000000000814"},
    {"nintendo_udsg-r_ext_zh-cn_003", "0100000000000814"},
    {"nintendo_udjxh-db_zh-tw_003", "0100000000000813"},
    {"nintendo_udsg-r_ko_003", "0100000000000812"},
    {"nintendo_ext_003", "0100000000000810"},
};
static uint8_t *g_shm; static uint32_t g_off[NFONT], g_size[NFONT]; static int g_have[NFONT];
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static const char *font_dir(char *buf, size_t n) {
    const char *e = getenv("DM_SYSFONT"); if (e) return e;
    snprintf(buf, n, "%s%s/data/switch/sysfont", getenv("DM_ROOT") ?: getenv("HOME"), getenv("DM_ROOT") ? "" : "/dm-work"); return buf;
}
static uint8_t *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    struct stat st; if (fstat(fileno(f), &st)) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)st.st_size); *n = fread(b, 1, (size_t)st.st_size, f); fclose(f);
    if (*n != (size_t)st.st_size) { free(b); return NULL; }
    return b;
}
// BFTTF → TTF（就地）；返回 TTF 字节数，格式不对返回 0 | BFTTF → TTF (in place); returns TTF bytes, 0 if the format is wrong
static size_t bfttf_decrypt(uint8_t *b, size_t n) {
    if (n < 8 || n % 4) return 0;
    uint32_t w; memcpy(&w, b, 4); if ((w ^ 0x06186249u) != 0x18029a7fu) return 0;
    for (size_t i = 8; i < n; i += 4) { memcpy(&w, b + i, 4); w ^= 0x06186249u; memcpy(b + i - 8, &w, 4); }
    return n - 8;
}
static void pl_load(void) {
    g_shm = calloc(1, PL_SHMEM);
    char db[1024]; const char *dir = font_dir(db, sizeof db); uint32_t off = 0;
    for (int t = 0; t < NFONT; t++) {
        char p[1200]; size_t n = 0; uint8_t *b; size_t ttf = 0;
        snprintf(p, sizeof p, "%s/%s.bfttf", dir, g_font[t].file);
        if ((b = read_all(p, &n))) { if (!(ttf = bfttf_decrypt(b, n))) LOG("【红】pl：%s 不是 BFTTF（魔数不符）", p); }
        else { snprintf(p, sizeof p, "%s/%s.ttf", dir, g_font[t].file); if ((b = read_all(p, &n))) ttf = n; }
        if (!ttf) { free(b); continue; }
        if (off + 8 + ttf > PL_SHMEM) { LOG("【红】pl：字体总量超过共享内存 %#x 字节，%s 未放入", PL_SHMEM, g_font[t].file); free(b); continue; }
        uint32_t magic = 0x18029a7fu, esz = __builtin_bswap32((uint32_t)ttf ^ 0x49621806u);
        memcpy(g_shm + off, &magic, 4); memcpy(g_shm + off + 4, &esz, 4); memcpy(g_shm + off + 8, b, ttf); free(b);
        g_off[t] = off + 8; g_size[t] = (uint32_t)ttf; g_have[t] = 1; off += 8 + (uint32_t)ttf;
        LOG("pl：共享字体 %d %s %u 字节（偏移 %#x）", t, g_font[t].file, (unsigned)ttf, g_off[t]);
    }
}
static int font_ok(int t) {
    pthread_once(&g_once, pl_load);
    if (t < 0 || t >= NFONT) die("nn::pl 共享字体类型越界 %d", t);
    if (!g_have[t]) { char db[1024]; die("nn::pl 共享字体 %d 缺文件：%s/%s.bfttf（系统数据 %s 的 RomFS，从自己的主机导出）", t, font_dir(db, sizeof db), g_font[t].file, g_font[t].title); }
    return 1;
}
HLE_FORCE_OPT("_ZN2nn2pl22GetSharedFontLoadStateENS0_14SharedFontTypeE", "共享字体载入状态：宿主读盘即载入（1）", int, h_pl_LoadState, int t) { return font_ok(t); }
HLE_FORCE_OPT("_ZN2nn2pl20GetSharedFontAddressENS0_14SharedFontTypeE", "共享字体地址：宿主共享内存块里的明文 TTF", const void *, h_pl_Address, int t) { font_ok(t); return g_shm + g_off[t]; }
HLE_FORCE_OPT("_ZN2nn2pl17GetSharedFontSizeENS0_14SharedFontTypeE", "共享字体大小：TTF 字节数", size_t, h_pl_Size, int t) { font_ok(t); return g_size[t]; }
