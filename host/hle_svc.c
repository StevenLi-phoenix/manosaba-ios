// Horizon kernel system calls the SDK runtime makes: memory layout, heap, queries, debug output, timing, process info.
// Register convention as in the public Horizon ABI: inputs w1.., outputs w0 = Result, x1 = value (GetInfo / SetHeapSize).
#include "dm.h"
#include "hle.h"
#include "game.h"
#include <stdatomic.h>
#include <sys/mman.h>
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>

// One reserved block of guest address space, partitioned the Horizon way (address values are the host's real mmap addresses). It takes address space, not memory.
// Unity never calls SetHeapSize: its memory comes from the alias region through the SDK's VammManager, which keeps the first 0x3fe00000 of that region for its
// own node heap. A real 39-bit process has a 64 GB alias region; here Mac reserves 12 GB (alias 4 GB) and iOS steps down from 6 GB (see mem_init).
// DM_GUEST_AS_GB=<n> (n >= 6) sets the total.
static size_t g_reserve;

#define RESERVE g_reserve
static uint8_t *g_as;              // ASLR region base = the whole block
static uint8_t *g_heap; static size_t g_heap_cur, g_heap_cap; static int g_heap_rejects;
// The address range lies entirely within the currently opened (readable and writable) part of the guest heap: such reads need no kernel trap to check, memcpy directly (used where guest memory is read every tick, such as diag.c tk_rd)
int dm_guest_heap_readable(uint64_t a, size_t n) { uint64_t b = (uint64_t)(uintptr_t)g_heap; size_t cur = g_heap_cur; return g_heap && a >= b && n <= cur && a - b <= cur - n; }
static uint8_t *g_alias; static size_t g_alias_cap;
static uint8_t *g_stack; static size_t g_stack_cap;

// Physical memory in the alias region (SDK VammManager): Horizon maps it in 4 KB pages, the host page is 16 KB on Apple silicon. A host page is
// accessible while any of its 4 KB subpages is mapped. An unmapped subpage is zeroed while its host page stays in use; a host page with no mapped
// subpage left is replaced by a fresh anonymous PROT_NONE page, so a later map always reads zero (fresh physical memory on the real device).
static uint8_t *g_alias_bits; static pthread_mutex_t g_alias_lock = PTHREAD_MUTEX_INITIALIZER;   // one bit per 4 KB page of the alias region
static size_t g_alias_mapped, g_alias_hpages;   // mapped 4 KB pages; host pages with at least one mapped subpage (a whole host page counts toward memory once touched)
static int alias_bit(size_t i) { return g_alias_bits[i >> 3] >> (i & 7) & 1; }
// Alias region readout (any thread, no lock: two word reads): MB the game has mapped, and MB of host pages that mapping keeps in use; the difference
// is what 4 KB mappings scattered over 16 KB host pages cost on top
void dm_alias_stat(size_t *mapped_mb, size_t *host_mb) { *mapped_mb = g_alias_mapped >> 8; *host_mb = g_alias_hpages * vm_page_size >> 20; }
// Returns 1 (and maps nothing) when mapping would take more than limit_pages 4 KB pages in all: the console's kernel reserves the pages not yet mapped
// from the process's physical memory limit and fails with LimitReached (mesosphere KPageTableBase::MapPhysicalMemory)
static int alias_phys(uint8_t *a, size_t size, int map, size_t limit_pages) {
    size_t hp = vm_page_size, sub = hp / 0x1000;
    pthread_mutex_lock(&g_alias_lock);
    if (!g_alias_bits && !(g_alias_bits = calloc(g_alias_cap / 0x1000 / 8 + 1, 1))) die("cannot allocate the alias region bitmap");
    size_t first = (size_t)(a - g_alias) / 0x1000, n = size / 0x1000;
    if (map) { size_t add = 0; for (size_t i = first; i < first + n; i++) add += !alias_bit(i);
               if (g_alias_mapped + add > limit_pages) { pthread_mutex_unlock(&g_alias_lock); return 1; } }
    // Usage readout: mapped 4 KB pages, peak, highest offset used; one log line each time the peak passes another 128 MB (sizes the alias region; the iOS reservation has a ceiling)
    static size_t peak, top, logged;
    for (size_t i = first; i < first + n; i++) { int was = alias_bit(i); if (map && !was) g_alias_mapped++; else if (!map && was) g_alias_mapped--; }
    if (map && first + n > top) top = first + n;
    for (size_t h = first / sub * sub; h < first + n; h += sub) {   // every host page the range touches
        int before = 0, used = 0; for (size_t k = h; k < h + sub; k++) before |= alias_bit(k);
        for (size_t k = h > first ? h : first; k < h + sub && k < first + n; k++) if (map) g_alias_bits[k >> 3] |= 1 << (k & 7); else g_alias_bits[k >> 3] &= ~(1 << (k & 7));
        for (size_t k = h; k < h + sub; k++) used |= alias_bit(k);
        if (used && !before) g_alias_hpages++; else if (!used && before) g_alias_hpages--;
        uint8_t *hpage = g_alias + h * 0x1000;
        if (map) { if (mprotect(hpage, hp, PROT_READ | PROT_WRITE)) die("MapPhysicalMemory mprotect %p errno=%d", hpage, errno); }
        else if (used) { for (size_t k = h; k < h + sub; k++) if (k >= first && k < first + n) memset(g_alias + k * 0x1000, 0, 0x1000); }
        else if (mmap(hpage, hp, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) die("UnmapPhysicalMemory: replacing page %p failed, errno=%d", hpage, errno);
    }
    if (g_alias_mapped > peak) { peak = g_alias_mapped; if ((peak >> 15) > logged) { logged = peak >> 15;
        LOG("alias region peak use %zu MB (highest offset %zu MB, region %zu MB; host pages in use now %zu MB)", peak >> 8, top >> 8, g_alias_cap >> 20, g_alias_hpages * hp >> 20); } }
    pthread_mutex_unlock(&g_alias_lock);
    return 0;
}

static void mem_init(void) {
    if (g_as) return;
    size_t alias_cap = TARGET_OS_IPHONE ? 0xC0000000ull : 0x100000000ull;
    g_reserve = TARGET_OS_IPHONE ? 6ull << 30 : 12ull << 30;
    const char *e = getenv("DM_GUEST_AS_GB"); if (e && atoi(e) >= 6) g_reserve = (size_t)atoi(e) << 30;
#if TARGET_OS_IPHONE
    // Without the extended virtual addressing entitlement (a free account cannot sign it) the largest single reservation varies by device and OS:
    // 6 GB already fails on an iPhone 17 Pro (iOS 26.4). Step down from 6 GB by 256 MB until it succeeds; everything except the 256 MB prefix, the
    // 256 MB stack region and the profile's heap (guest_heap_mb) goes to the alias region (manosaba on the Mac: a 90 s story run peaks at 1166 MB
    // of alias used, highest offset 2460 MB).
    if (!e) {
        for (size_t r = g_reserve; r >= 0x80000000ull; r -= 0x10000000ull) {
            g_as = mmap(NULL, r, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (g_as != MAP_FAILED) { g_reserve = r; break; }
            g_as = NULL;
        }
        if (!g_as) die("reserve guest AS: not even 2 GB could be reserved");
        size_t heap = (size_t)(dm_game.guest_heap_mb ? dm_game.guest_heap_mb : 256) << 20;
        if (heap > g_reserve - 0x80000000ull) die("reserve guest AS: %zu MB cannot hold a %zu MB heap", g_reserve >> 20, heap >> 20);
        alias_cap = g_reserve - 0x20000000ull - heap;
        LOG("guest address space: iOS reserved %zu MB (alias %zu MB)", g_reserve >> 20, alias_cap >> 20);
        if (alias_cap < 0x60000000ull) die("reserve guest AS: only %zu MB, alias region %zu MB is too small (VammManager keeps ~1 GB)", g_reserve >> 20, alias_cap >> 20);
    }
#endif
    if (!g_as) g_as = mmap(NULL, RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_as == MAP_FAILED) die("reserve guest AS %#zx", RESERVE);
    // Partition inside the block: 256 MB prefix, alias region, stack region 256 MB, heap the rest
    g_alias = g_as + 0x10000000; g_alias_cap = alias_cap;
    g_stack = g_alias + g_alias_cap; g_stack_cap = 0x10000000;
    g_heap  = g_stack + g_stack_cap; g_heap_cap  = RESERVE - (size_t)(g_heap - g_as);
    LOG("guest AS %p +%#llx  heap %p alias %p stack %p", g_as, (unsigned long long)RESERVE, g_heap, g_alias, g_stack);
}

// Application memory pool reported to the game: the 3285 MB a 4 GB Switch gives applications, + DM_APP_POOL_ADD_MB (default 512; may be negative).
// GetInfo TotalMemory reports it and SetHeapSize refuses a heap that does not fit beside the loaded images.
#define APP_POOL 0xCD500000ull
static uint64_t app_pool(void) {
    static uint64_t v;
    if (!v) { const char *e = getenv("DM_APP_POOL_ADD_MB"); long add = e ? atol(e) : 512; v = APP_POOL + ((uint64_t)add << 20);
              if (e) LOG("DM_APP_POOL_ADD_MB: application pool %llu MB (Switch 3285 MB %+ld)", (unsigned long long)(v >> 20), add); }
    return v;
}
static size_t image_bytes(void) { size_t n = 0; for (int i = 0; i < g_nmod; i++) n += g_mod[i].size; return (n + 0x1fffff) & ~(size_t)0x1fffff; }

static module_t *mod_named(const char *n) { for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, n)) return &g_mod[i]; return NULL; }
// The SDK's own allocator, called as is: a null return is logged (first 8), since callers in games often do not check it
HLE_FORCE("_ZN2nn3mem17StandardAllocator8AllocateEmm", "the SDK allocator, logging null returns", void *, hle_stdalloc_allocate, void *self, size_t size, size_t align) {
    static void *(*orig)(void *, size_t, size_t);
    if (!orig) { orig = (void *(*)(void *, size_t, size_t))module_sym(mod_named("sdk"), "_ZN2nn3mem17StandardAllocator8AllocateEmm"); if (!orig) die("StandardAllocator::Allocate not found in sdk"); }
    void *p = orig(self, size, align);
    if (!p && size) { static _Atomic int n; if (atomic_fetch_add(&n, 1) < 8) LOG("[red] game heap allocation returned null: %zu bytes (alignment %zu), allocator %p", size, align, self); }
    return p;
}

// For memory accounting (memstat.m): the range of the whole guest address space block (base = NULL when not initialized)
void svc_guest_range(uint8_t **base, size_t *size) { *base = g_as; *size = RESERVE; }
// The parts of that block (memstat.m splits the guest's memory by them): alias region (physical memory the game maps), stack region (MapMemory), heap
void svc_guest_layout(uint8_t **alias, size_t *alias_n, uint8_t **stack, size_t *stack_n, uint8_t **heap, size_t *heap_n) { *alias = g_alias; *alias_n = g_alias_cap; *stack = g_stack; *stack_n = g_stack_cap; *heap = g_heap; *heap_n = g_heap_cap; }

enum { I_CoreMask=0, I_PriorityMask=1, I_AliasAddr=2, I_AliasSize=3, I_HeapAddr=4, I_HeapSize=5,
       I_TotalMem=6, I_UsedMem=7, I_DebuggerAttached=8, I_ResourceLimit=9, I_IdleTick=10, I_RandomEntropy=11,
       I_AslrAddr=12, I_AslrSize=13, I_StackAddr=14, I_StackSize=15, I_SysResTotal=16, I_SysResUsed=17,
       I_ProgramId=18, I_InitProcIdRange=19, I_UserExcAddr=20, I_TotalNonSys=21, I_UsedNonSys=22, I_IsApplication=23,
       I_FreeThreadCount=24, I_AliasExtraSize=28 };

static uint8_t g_seen[256];

// Registry of mapped ranges (MapMemory stacks, MapPhysicalMemory alias pages), QueryMemory reports from it; the SDK relies on it to find free space
#define MAX_MAP 4096
static struct { uint64_t base, size, src; uint32_t state; } g_map[MAX_MAP];
static int g_nmap;
static pthread_mutex_t g_map_lock = PTHREAD_MUTEX_INITIALIZER;
static int map_find(uint64_t a, uint64_t n) {       // returns a 1-based index, 0 = no overlap
    for (int i = 0; i < g_nmap; i++) if (a < g_map[i].base + g_map[i].size && g_map[i].base < a + n) return i + 1;
    return 0;
}
static void map_add(uint64_t b, uint64_t n, uint64_t src, uint32_t st) {
    pthread_mutex_lock(&g_map_lock);
    if (g_nmap >= MAX_MAP) die("mapping table full");
    g_map[g_nmap++] = (typeof(g_map[0])){b, n, src, st};
    pthread_mutex_unlock(&g_map_lock);
}
static void map_del(int i) { pthread_mutex_lock(&g_map_lock); g_map[i] = g_map[--g_nmap]; pthread_mutex_unlock(&g_map_lock); }
static pthread_mutex_t g_mapmem_mu = PTHREAD_MUTEX_INITIALIZER;   // MapMemory / UnmapMemory as a whole (check, copy, register, release pages)

int svc_thread(int num, uint64_t *regs);   // hle_thread.c: thread and synchronization kind

// Host calls (svc 0x80–0xff, hle.h): handlers registered by the game's hook code
static int (*g_hcall[0x80])(uint64_t *regs);
void dm_host_call_register(int num, int (*fn)(uint64_t *regs)) {
    if (num < 0x80 || num > 0xff) die("host call number %#x out of range (0x80–0xff)", num);
    if (g_hcall[num - 0x80] && g_hcall[num - 0x80] != fn) die("host call %#x registered twice", num);
    g_hcall[num - 0x80] = fn;
}

uint64_t svc_dispatch(int num, uint64_t *regs, uint64_t orig_addr) {
    { void hlecount_svc(int); hlecount_svc(num); }   // DM_HLECOUNT: svc counts by number
    mem_init();
    if (num >= 0x80 && num <= 0xff) {
        if (g_hcall[num - 0x80] && g_hcall[num - 0x80](regs)) return regs[0];
        die("host call %#x from %s has no handler (the product's cuts and the game's hooks disagree)", num, module_of((void *)orig_addr) ? module_of((void *)orig_addr)->name : "?");
    }
    if (svc_thread(num, regs)) return regs[0];
    switch (num) {
    case 0x01: {  // SetHeapSize(out_addr*, size)  → x1 = heap base
        size_t size = regs[1];
        if (size > g_heap_cap) { LOG("SetHeapSize OOM: req %#zx > cap %#zx", size, g_heap_cap); regs[0] = 0xD401; return 0; }
        if (size + image_bytes() > app_pool()) { g_heap_rejects++; regs[0] = 0x10801; return 0; }   // exceeds the application memory pool: ResultLimitReached. At startup the game retries in 2 MB decrements until it fits (main+0x1b66184)
        int rc = 0;
        if (size > g_heap_cur) rc = mprotect(g_heap + g_heap_cur, size - g_heap_cur, PROT_READ | PROT_WRITE);
        else if (size < g_heap_cur) rc = mprotect(g_heap + size, g_heap_cur - size, PROT_NONE);
        if (g_flag.svc) LOG("SetHeapSize %#zx -> base %p mprotect=%d errno=%d", size, g_heap, rc, errno);
        { static int said; if (!said && size) { said = 1; LOG("SetHeapSize: heap first set to %zu MB (application pool %llu MB − images %zu MB; %d refusals before)", size >> 20, (unsigned long long)(app_pool() >> 20), image_bytes() >> 20, g_heap_rejects); } }
        g_heap_cur = size;
        regs[0] = 0; regs[1] = (uint64_t)g_heap; return 0;
    }
    case 0x02: regs[0] = 0; return 0;   // SetMemoryPermission
    case 0x03: regs[0] = 0; return 0;   // SetMemoryAttribute
    case 0x04: {  // MapMemory(dst, src, size): when creating a thread the SDK moves the stack on the heap into the stack region
        uint64_t dst = regs[0], src = regs[1], size = regs[2];
        pthread_mutex_lock(&g_mapmem_mu);   // with UnmapMemory: a 16 KB page can hold the ends of two stacks, created and retired on different threads
        if ((uint8_t *)dst < g_stack || (uint8_t *)dst + size > g_stack + g_stack_cap || map_find(dst, size)) {
            pthread_mutex_unlock(&g_mapmem_mu); LOG("MapMemory: target invalid or in use %#llx+%#llx", dst, size); regs[0] = 0xDC01; return 0; }
        // Apple pages are 16K, Horizon pages 4K: the accessible region is extended to a 16K boundary (the extra pages opened are unused pages of the same region, semantics unaffected)
        uint64_t lo = dst & ~0x3FFFull, hi = (dst + size + 0x3FFF) & ~0x3FFFull;
        if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE)) die("MapMemory mprotect errno=%d", errno);
        memcpy((void *)dst, (void *)src, size);   // move semantics: the source region is not accessed while mapped (Horizon makes it inaccessible)
        map_add(dst, size, src, 0x0B /*MappedMemory*/);
        pthread_mutex_unlock(&g_mapmem_mu);
        if (g_flag.svc) LOG("MapMemory %#llx <- %#llx +%#llx", dst, src, size);
        regs[0] = 0; return 0;
    }
    case 0x05: {  // UnmapMemory(dst, src, size): contents are moved back to the source region; the host pages no other mapping shares are released (below)
        uint64_t dst = regs[0], src = regs[1], size = regs[2];
        pthread_mutex_lock(&g_mapmem_mu);
        int k = map_find(dst, size);
        if (!k || g_map[k - 1].base != dst || g_map[k - 1].size != size || g_map[k - 1].src != src) {
            pthread_mutex_unlock(&g_mapmem_mu); LOG("UnmapMemory: does not match a mapping %#llx+%#llx", dst, size); regs[0] = 0xD401; return 0; }
        memcpy((void *)src, (void *)dst, size);
        map_del(k - 1);
        // Give the pages back: on the console the stack region only aliases the heap's memory, here it holds a copy, which used to stay resident
        // after the thread ended (Unity's thread pool comes and goes at every scene load: about 6 MB more per load). A host page no other mapping
        // touches is replaced with a fresh inaccessible one, as an unmapped page is on the console; a page shared with a neighbour stays as it is
        for (uint64_t h = dst & ~(uint64_t)(vm_page_size - 1); h < dst + size; h += vm_page_size)
            if (!map_find(h, vm_page_size) && mmap((void *)h, vm_page_size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) die("UnmapMemory: replacing page %#llx failed, errno=%d", h, errno);
        pthread_mutex_unlock(&g_mapmem_mu);
        regs[0] = 0; return 0;
    }
    case 0x06: {  // QueryMemory(MemoryInfo* out, PageInfo* pout, addr) → x1 = pageinfo
        uint64_t mi = regs[0], addr = regs[2];
        // MemoryInfo: base(0) size(8) state(0x10) attr(0x14) perm(0x18) ...
        uint8_t *a = (uint8_t *)addr;
        uint64_t base, size, state, perm;
        int k;
        if (a >= g_heap && a < g_heap + g_heap_cur) { base=(uint64_t)g_heap; size=g_heap_cur; state=5/*Heap*/; perm=3; }
        else if ((k = map_find(addr, 1))) { base=g_map[k-1].base; size=g_map[k-1].size; state=g_map[k-1].state; perm=3; }
        else if (a >= g_as && a < g_as + RESERVE) {   // free: up to the adjacent mapping/heap boundary
            uint64_t lo = (uint64_t)g_as, hi = (uint64_t)g_as + RESERVE;
            if (a >= g_heap) lo = (uint64_t)g_heap + g_heap_cur;
            else if (hi > (uint64_t)g_heap) hi = (uint64_t)g_heap;
            for (int i = 0; i < g_nmap; i++) {
                uint64_t e = g_map[i].base + g_map[i].size;
                if (e <= addr && e > lo) lo = e;
                if (g_map[i].base > addr && g_map[i].base < hi) hi = g_map[i].base;
            }
            base=lo; size=hi-lo; state=0; perm=0; }
        else {
            // The address lies in "the current host thread's stack": report the whole stack (state 0xB = stack). When the SDK initializes the main thread record it queries here with the sp at that moment and takes the reported range as the main thread stack;
            // later nn::os::GetCurrentStackInfo asserts that sp is still inside that range and aborts if not. Previously every address outside the guest address space was reported as "the 4 KiB page containing it",
            // so the main thread stack was recorded as one page. Whether the sp at init and a later sp share a page depends on the absolute position of the host stack top, which moves with the total length of environment variables / launch arguments (a different environment occasionally aborts at startup).
            // This Diablo build does not call GetCurrentStackInfo (main / subsdk0 do not import it and the SDK has no internal call site; the host reaches the main menu with 12 different environment padding lengths), so it was never hit; reporting "one page" is wrong in itself, so fix it anyway.
            uint64_t top = (uint64_t)pthread_get_stackaddr_np(pthread_self()), ssz = pthread_get_stacksize_np(pthread_self());
            if (addr < top && addr >= top - ssz) { base = top - ssz; size = ssz; state = 0xB/*Stack*/; perm = 3; }
            else { base=addr & ~0xFFFull; size=0x1000; state=3/*Code*/; perm=5; } }
        if (mi) { uint64_t *m=(uint64_t*)mi; m[0]=base; m[1]=size; ((uint32_t*)mi)[4]=(uint32_t)state; ((uint32_t*)mi)[5]=0; ((uint32_t*)mi)[6]=(uint32_t)perm; ((uint32_t*)mi)[7]=0; ((uint32_t*)mi)[8]=0; }
        regs[0] = 0; regs[1] = 0; return 0;
    }
    case 0x2C: case 0x2D: {  // MapPhysicalMemory / UnmapPhysicalMemory(addr, size): only allowed inside the alias region (used by the SDK's VammManager)
        uint8_t *a = (uint8_t *)regs[0]; size_t size = regs[1];
        if (a < g_alias || a + size > g_alias + g_alias_cap) { LOG("SVC %#x outside the alias region %p+%#zx", num, a, size); regs[0] = 0xD401; return 0; }
        if (((uintptr_t)a | size) & 0xfff) die("SVC %#x: address / size not 4 KB aligned %p+%#zx (the SDK never does this)", num, a, size);
        // The physical memory the game may map: the application pool it is told about (GetInfo TotalMemory) less the images and the heap, as on the console
        size_t pool = (size_t)app_pool(), used = image_bytes() + g_heap_cur, limit = pool > used ? (pool - used) / 0x1000 : 0;
        if (alias_phys(a, size, num == 0x2C, limit)) {
            static _Atomic unsigned long long refused; unsigned long long k = atomic_fetch_add(&refused, 1) + 1; size_t mm, hm; dm_alias_stat(&mm, &hm);
            if (k <= 8 || !(k & (k - 1))) LOG("MapPhysicalMemory refused (LimitReached, #%llu): %zu KB more would pass the %zu MB the application pool leaves for it (%zu MB mapped)", k, size >> 10, limit >> 8, mm);
            regs[0] = 0x10801; return 0;
        }
        if (g_flag.svc) LOG("%sPhysicalMemory %p+%#zx", num == 0x2C ? "Map" : "Unmap", a, size);
        regs[0] = 0; return 0;
    }
    case 0x26: {  // Break(reason, ptr, size): SDK fatal abort
        uint32_t res = (regs[1] && regs[2] >= 4) ? *(uint32_t *)regs[1] : 0;
        module_t *mm = module_of((void *)regs[30]);
        static int nb;
        LOG("Break#%d reason=%#llx Result=%#x, svc at %s+%#llx", ++nb, regs[0], res, mm ? mm->name : "?", orig_addr);
        dm_backtrace(regs[29]);
        if (regs[0] & 0x80000000ull) return 0;   // notification only, can continue
        _exit(42);
    }
    case 0x1E: regs[0] = 0; regs[1] = guest_ns() * 12 / 625; return 0;   // GetSystemTick
    case 0x27: {  // OutputDebugString(str, len)
        if (g_flag.dbg) fprintf(stderr, "[guest] %.*s\n", (int)regs[1], (char *)regs[0]);
        regs[0] = 0; return 0;
    }
    case 0x29: {  // GetInfo(out*, id, handle, subid) → x1 = value
        uint64_t id = regs[1], v = 0;
        switch (id) {
        case I_CoreMask: v = 7; break;
        case I_PriorityMask: v = 0; break;
        case I_AliasAddr: v = (uint64_t)g_alias; break;   case I_AliasSize: v = g_alias_cap; break;
        case I_HeapAddr:  v = (uint64_t)g_heap;  break;   case I_HeapSize:  v = g_heap_cap;  break;
        case I_AslrAddr:  v = (uint64_t)g_as;    break;   case I_AslrSize:  v = RESERVE;     break;
        case I_StackAddr: v = (uint64_t)g_stack; break;   case I_StackSize: v = g_stack_cap; break;
        case I_TotalMem: case I_TotalNonSys: v = app_pool(); break;
        case I_UsedMem:  case I_UsedNonSys:  v = g_heap_cur + image_bytes(); break;   // Used = heap + image, the same accounting as the check in SetHeapSize (it used to be hard-coded to 64 MB). The sdk's QueryMemoryInfo (sdk+0x34f4ec) only uses it to compute the "program usage" field;
                                                 // The "total available" field is simply the pool total, so at startup the game still has to step down from the whole pool by 2 MB at a time until pool - image (about 34 refusals) when setting its heap -- this is how the original behaves on the real device, not a mistake in our accounting
        case I_SysResTotal: v = 0x2000000; break; case I_SysResUsed: v = 0x400000; break;
        case I_DebuggerAttached: v = 0; break;
        case I_RandomEntropy: { uint64_t dm_entropy64(void); v = dm_entropy64(); break; }   // a deterministic stream when DM_DETCLOCK is on (hle_os.c)
        case I_ProgramId: v = dm_game.program_id; break;
        case I_IsApplication: v = 1; break;
        case I_UserExcAddr: v = 0; break;
        // Threads the process may still create (Unity checks this before starting each worker and silently skips the thread at 0, then waits
        // forever for it). The host has no real limit besides its handle table; 128 is a generous cap of the host's own choosing
        case I_FreeThreadCount: { void thr_counts(int *, int *, int *); int nh, nl, no; thr_counts(&nh, &nl, &no); v = nl < 128 ? (uint64_t)(128 - nl) : 0; break; }
        case I_AliasExtraSize: v = 0; break;   // FW 18+: part of the alias region the kernel keeps for itself; none here
        default: v = 0; break;
        }
        regs[0] = 0; regs[1] = v; return 0;
    }
    default:
        if (!g_seen[num & 0xff]) { g_seen[num & 0xff] = 1; LOG("SVC %#x x0=%#llx x1=%#llx x2=%#llx (placeholder, returns success)", num, regs[0], regs[1], regs[2]); }
        regs[0] = 0; return 0;
    }
}
