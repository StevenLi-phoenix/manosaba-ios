// gfx/nvn/hle/cmdbuf.inc, part of hle_nvn.c (included there in order, one translation unit): Command buffers: recording into the game's command memory, CPU-visible side effects at submit
// ---------- Command buffer: record into the game's command memory, perform CPU-visible side effects at submit ----------
#define CB(c) ((cmd_t *)(c))
// Room kept at a segment's end after every record but the END: one JUMP (16 bytes), so a record that does not fit can always be linked onward. The
// END keeps none: it takes the last bytes (plan item 231, after nxnative 0b3c7b1, MIT). Keeping 24 after every record, the END included, sent the
// END of a list measured at 496 bytes in a 512-byte window to another segment, its JUMP at 488..504 reaching 8 bytes past the measured size: where
// the game places the next list at the measured size (Tears of the Kingdom's prerecorded model lists, nxnative issue #61), that list's BEGIN
// overwrote the JUMP's target, and the replay jumped into a record (0x1000000003) after 8-30 minutes of play
#ifndef DM_TEST_CMD_KEEP24   // negative control of t_nvn only: 24 kept after every record, the END included, as before
#define OP_KEEP 16
#define OP_KEEP_END 0
#else
#define OP_KEEP 24
#define OP_KEEP_END 24
#endif

NVN(nvnCommandBufferInitialize, NVNboolean, NVNcommandBuffer *c, NVNdevice *d) {
    memset(c, 0, sizeof(cmd_t)); CB(c)->magic = MAGIC('C', 'M', 'D', 'B'); CB(c)->dev = d; return 1;
}
NVN(nvnCommandBufferFinalize, void, NVNcommandBuffer *c) { CB(c)->magic = 0; }
NVN(nvnCommandBufferSetMemoryCallback, void, NVNcommandBuffer *c, PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC cb) { CB(c)->cb = cb; }
// bool argument: only the low 8 bits count (Switch callers do not extend it)
NVN_NOPROTO(nvnCommandBufferSetCommandMemoryCallbackEnabled, void, NVNcommandBuffer *c, unsigned on) { CB(c)->cb_off = !(on & 0xff); }
NVN(nvnCommandBufferSetMemoryCallbackData, void, NVNcommandBuffer *c, void *d) { CB(c)->cb_data = d; }
static uint8_t *align8(uint8_t *p) { return (uint8_t *)(((uintptr_t)p + 7) & ~(uintptr_t)7); }
// Diagnostic DM_NVN_CMDSTAT=1: every 300 frames log one line of command recording usage -- how many bytes / records were recorded per frame, how large a command memory segment the game gives each time, how many out-of-memory callbacks per frame, how many host-supplied segments,
// and the count and bytes of each command type (to find "how much larger our records are than real-device commands"). Counts only; does not change recording / replay
static struct { int on; uint64_t bytes, recs, grants, grant_bytes, grant_min, grant_max, cbs, hostseg, op_n[64], op_b[64], frame0, waste; } g_cs = { .on = -1 };
static pthread_mutex_t g_cs_mu = PTHREAD_MUTEX_INITIALIZER;
uint64_t dm_nvn_frame_count(void);
static int cs_on(void) { if (g_cs.on < 0) g_cs.on = getenv("DM_NVN_CMDSTAT") != NULL; return g_cs.on; }
static void cs_op(uint32_t op, uint32_t size) { if (!cs_on() || op >= 64) return; pthread_mutex_lock(&g_cs_mu); g_cs.op_n[op]++; g_cs.op_b[op] += size; pthread_mutex_unlock(&g_cs_mu); }
static void cs_dump(void) {   // Called once per frame (at present)
    if (!cs_on()) return; uint64_t f = dm_nvn_frame_count(); if (f - g_cs.frame0 < 300) return;
    pthread_mutex_lock(&g_cs_mu); double n = (double)(f - g_cs.frame0); char top[700]; int k = 0; uint8_t used[64] = {0};
    for (int r = 0; r < 8; r++) { int bi = -1; for (int i = 0; i < 64; i++) if (!used[i] && g_cs.op_b[i] && (bi < 0 || g_cs.op_b[i] > g_cs.op_b[bi])) bi = i; if (bi < 0) break; used[bi] = 1;
        k += snprintf(top + k, sizeof top - k, " op%d: %.0f recs x %.0f bytes=%.1f KB;", bi, g_cs.op_n[bi] / n, (double)g_cs.op_b[bi] / (double)g_cs.op_n[bi], g_cs.op_b[bi] / n / 1024.0); }
    LOG("[nvn] command memory up to frame %llu: recorded per frame %.1f KB / %.0f recs; game segments per frame %.2f, avg %.1f KB each (min %.1f / max %.1f); out-of-memory callbacks per frame %.2f; host segments per frame %.3f; segment tails discarded on switch per frame %.1f KB | per frame:%s",
        (unsigned long long)f, g_cs.bytes / n / 1024.0, g_cs.recs / n, g_cs.grants / n, g_cs.grants ? g_cs.grant_bytes / (double)g_cs.grants / 1024.0 : 0, g_cs.grant_min / 1024.0, g_cs.grant_max / 1024.0, g_cs.cbs / n, g_cs.hostseg / n, g_cs.waste / n / 1024.0, top);
    int on = g_cs.on; memset(&g_cs, 0, sizeof g_cs); g_cs.on = on; g_cs.frame0 = f; pthread_mutex_unlock(&g_cs_mu);
}
static void put_hdr(uint8_t *p, uint32_t op, uint32_t size) { op_hdr h = {op, size}; memcpy(p, &h, sizeof h); if (op) cs_op(op, size); }
// While recording, link the old segment to the next one with a JUMP at the write position. The JUMP counts as used: the game places what it
// records next after the bytes GetCommandMemoryUsed reports, and a JUMP left outside them was overwritten (its target first: the case where only
// END went on to the next segment reported 8 bytes less than the recording took). A recording with no record yet (its BEGIN did not fit) is not
// linked: the write position is then after the previous recording's END, outside anything this one reports, and its handle is its BEGIN anyway
static void cmd_link(cmd_t *o, uint8_t *to) {
    if (cs_on() && o->end > o->cur) { pthread_mutex_lock(&g_cs_mu); g_cs.waste += (uint64_t)(o->end - o->cur); pthread_mutex_unlock(&g_cs_mu); }
    put_hdr(o->cur, OP_JUMP, 16); memcpy(o->cur + 8, &to, 8); o->used += 16;
}
// New segment of the game's command memory
static void cmd_new_segment(cmd_t *o, uint8_t *mem, size_t n) {
    uint8_t *start = align8(mem), *end = mem + n;
#ifndef DM_TEST_CMD_LINK_EMPTY   // negative control of t_nvn only: a recording linked before its BEGIN, as before
    if (o->recording && o->cur && o->begin) cmd_link(o, start);
#else
    if (o->recording && o->cur) cmd_link(o, start);
#endif
    o->cur = start; o->end = end;
}
// Host-supplied segment when the game's own command memory runs out: when the game's out-of-memory callback cannot allocate from its command memory pool, it calls AddCommandMemory with an empty pool (p = NULL, n = 0)
// (opening the advanced stats page on the level 88 sorceress save: many list items in one frame, and the original implementation segfaulted here reading NULL+8). Our records are larger than real-device NVN commands, so the budget the game reserved for real-device usage is not necessarily enough,
// so this is not an error state of the game but something we must catch: provide a segment of host memory to keep recording; the segment is replayed in place at submit.
// Reclaim follows the game's command memory, not a frame count (nxnative, seedds, MIT): a game may keep a recorded command handle and CALL it every frame until it records it again, so a segment reclaimed
// 8 frames after lending was rewritten while still in use (nxnative saw it in Super Mario Bros. Wonder). A segment is linked from one place, the JUMP the recording wrote when it ran out of room, and lives
// as long as that JUMP: it sits in the game's command memory (or in the host segment the recording overflowed from) within the bytes GetCommandMemoryUsed reported, which the game writes over only once
// the recording is dead. So a segment is free once its JUMP is gone: overwritten, in a memory pool that no longer exists, or in a host segment that is free or was taken again. A segment a recording began
// in (an empty pool given before BeginRecording) has no JUMP and is kept. Reclaiming when the game merely gave out memory overlapping the recording (until 2026-10-18) freed live segments: Tears of the
// Kingdom gives each per-draw recording a 512-byte window at slot + used, which reaches into the next slots' recordings; a later recording took the segment, and its binds became the tail of a cached
// draw handle the game calls every other frame (in the prologue the wrong skeleton buffer, and Zelda's cloak and face alternated). The pool grows on demand; abort only beyond HOSTSEG_CAP in total.
#define HOSTSEG_SZ (256u << 10)
#define HOSTSEG_CAP (1u << 30)
uint64_t dm_nvn_frame_count(void);
// jump_at: the JUMP linking it (0 = none); parent: the host segment that JUMP is in (-1 = game memory) and that segment's gen at the time (gen counts takes)
typedef struct { uint8_t *mem; uintptr_t jump_at; int busy, parent; uint32_t gen, parent_gen; } hostseg_t;
static hostseg_t *g_hostseg; static int g_nhostseg, g_caphostseg;
static uint64_t g_hostseg_freed;   // segments found free because their JUMP was gone
static pthread_mutex_t g_hostseg_mu = PTHREAD_MUTEX_INITIALIZER;
static int hostseg_linked(int i, int depth) {   // g_hostseg_mu held
    const hostseg_t *s = &g_hostseg[i];
    if (!s->busy) return 0;
    if (!s->jump_at) return 1;
    if (s->parent >= 0) { if (depth > 64 || g_hostseg[s->parent].gen != s->parent_gen || !hostseg_linked(s->parent, depth + 1)) return 0; }
    else if (!mtl_pool_has(s->jump_at, 16)) return 0;   // its pool is gone (and the memory may be too: read only inside a live pool)
    op_hdr h; uint64_t to; memcpy(&h, (const void *)s->jump_at, sizeof h); memcpy(&to, (const void *)(s->jump_at + 8), 8);
    return h.op == OP_JUMP && h.size == 16 && to == (uint64_t)(uintptr_t)s->mem;
}
// Continue o in a host segment: a free one, else one whose JUMP is gone, else a new one; linked from the write position under the lock (no other
// thread sees it taken but not yet linked)
static void cmd_host_segment(cmd_t *o) {
    static unsigned total; uint64_t f = dm_nvn_frame_count();
#ifndef DM_TEST_CMD_LINK_EMPTY
    uintptr_t at = o->recording && o->cur && o->begin ? (uintptr_t)o->cur : 0;   // (no JUMP for a recording with no record yet: cmd_link)
#else
    uintptr_t at = o->recording && o->cur ? (uintptr_t)o->cur : 0;
#endif
    pthread_mutex_lock(&g_hostseg_mu);
    int parent = -1, k = -1;
    for (int i = 0; i < g_nhostseg && at; i++) if (at >= (uintptr_t)g_hostseg[i].mem && at < (uintptr_t)g_hostseg[i].mem + HOSTSEG_SZ) { parent = i; break; }
    for (int i = 0; i < g_nhostseg && k < 0; i++) if (!g_hostseg[i].busy) k = i;
    if (k < 0)   // sweep: free every segment whose JUMP is gone (a segment freed here frees the ones it links at the next sweep)
        for (int i = 0; i < g_nhostseg; i++) if (i != parent && g_hostseg[i].busy && !hostseg_linked(i, 0)) { g_hostseg[i].busy = 0; g_hostseg_freed++; if (k < 0) k = i; }
    if (k < 0 && (uint64_t)(g_nhostseg + 1) * HOSTSEG_SZ <= HOSTSEG_CAP) {
        if (g_nhostseg == g_caphostseg) { g_caphostseg = g_caphostseg ? g_caphostseg * 2 : 64; g_hostseg = realloc(g_hostseg, sizeof *g_hostseg * (size_t)g_caphostseg); }
        uint8_t *m = malloc(HOSTSEG_SZ); if (!m) die("NVN command memory: host segment allocation failed");
        g_hostseg[g_nhostseg] = (hostseg_t){ m, 0, 0, -1, 0, 0 }; k = g_nhostseg++;
    }
    if (k < 0) die("NVN command memory: game pool empty and %u MB of host segments all linked from live recordings", HOSTSEG_CAP >> 20);
    hostseg_t *s = &g_hostseg[k];
    s->busy = 1; s->gen++; s->jump_at = at; s->parent = parent; s->parent_gen = parent >= 0 ? g_hostseg[parent].gen : 0;
    uint8_t *m = s->mem;
    if (at) cmd_link(o, m);
    unsigned t = ++total; int n = g_nhostseg; uint64_t freed = g_hostseg_freed;
    pthread_mutex_unlock(&g_hostseg_mu);
    o->cur = m; o->end = m + HOSTSEG_SZ;
    if (t == 1 || t % 1024 == 0) LOG("[nvn] game command memory short, host segment supplied (total %u, frame %llu, pool %d x %u KB, %llu freed by their JUMP being gone)", t, (unsigned long long)f, n, HOSTSEG_SZ >> 10, (unsigned long long)freed);
}
NVN(nvnCommandBufferAddCommandMemory, void, NVNcommandBuffer *c, const NVNmemoryPool *p, ptrdiff_t off, size_t n) {
    const pool_t *pl = (const pool_t *)p;
    if (!pl || n < 64) { if (cs_on()) { pthread_mutex_lock(&g_cs_mu); g_cs.hostseg++; pthread_mutex_unlock(&g_cs_mu); } cmd_host_segment(CB(c)); return; }
    if (!pl->mem) { LOG("[red] AddCommandMemory: pool has no CPU storage"); return; }
    if (cs_on()) { pthread_mutex_lock(&g_cs_mu); g_cs.grants++; g_cs.grant_bytes += n; if (!g_cs.grant_min || n < g_cs.grant_min) g_cs.grant_min = n; if (n > g_cs.grant_max) g_cs.grant_max = n; pthread_mutex_unlock(&g_cs_mu); }
    cmd_new_segment(CB(c), pl->mem + off, n);
}
NVN(nvnCommandBufferAddControlMemory, void, NVNcommandBuffer *c, void *m, size_t n) { (void)c; (void)m; (void)n; }
// Get space for one record and `keep` bytes after it (OP_KEEP, the END OP_KEEP_END); if not enough, call back into the game per the NVN convention
// to add command memory
static uint8_t *cmd_alloc_keep(cmd_t *o, uint32_t size, uint32_t keep) {
    size = (size + 7) & ~7u;
    for (int tries = 0; !o->cur || o->cur + size + keep > o->end; tries++) {
        // Callback turned off (TotK main 0xbb5a0): the game sized this buffer's memory for console commands, which are smaller than our records, and will not be
        // asked for more; the host supplies a segment, as for an empty pool
        // or the callback cannot supply enough (newer-SDK games hand out small chunks sized for console commands): continue in a host segment (nxnative)
        if (o->cb_off || !o->cb || tries == 2) {
            if (size + keep > HOSTSEG_SZ) die("NVN record of %u bytes does not fit a host segment", size);
            if (!o->cb_off) { static int once; if (!once++) LOG("[nvn] command memory: the game's callback cannot supply enough (cb=%p, %u bytes needed), host segments instead", (void *)o->cb, size + keep); }
            cmd_host_segment(o); break;
        }
        if (cs_on()) { pthread_mutex_lock(&g_cs_mu); g_cs.cbs++; pthread_mutex_unlock(&g_cs_mu); }
        o->cb((NVNcommandBuffer *)o, NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY, size + keep, o->cb_data);
    }
    if (cs_on()) { pthread_mutex_lock(&g_cs_mu); g_cs.bytes += size; g_cs.recs++; pthread_mutex_unlock(&g_cs_mu); }
    o->used += size;
    uint8_t *p = o->cur; o->cur += size; put_hdr(p, 0, size); return p;
}
static uint8_t *cmd_alloc(cmd_t *o, uint32_t size) { return cmd_alloc_keep(o, size, OP_KEEP); }
NVN(nvnCommandBufferBeginRecording, void, NVNcommandBuffer *c) {
    cmd_t *o = CB(c);
    if (o->recording) LOG("[red] BeginRecording re-entered");
    o->recording = 1; o->used = 0; o->begin = NULL;   // (no record yet: a segment change before the BEGIN is not linked)
    uint8_t *p = cmd_alloc(o, 8); put_hdr(p, OP_BEGIN, 8); o->begin = p;
}
// Memory used by this recording (TotK main 0x8c7ccc asks after recording; nxnative found that a game treats 0 as an empty recording): record bytes over all
// segments and the JUMPs linking them (cmd_link); the host uses no control memory
NVN(nvnCommandBufferGetCommandMemoryUsed, size_t, const NVNcommandBuffer *c) { const cmd_t *m = (const cmd_t *)c; return m->begin ? (size_t)m->used : 0; }
NVN(nvnCommandBufferGetControlMemoryUsed, size_t, const NVNcommandBuffer *c) { (void)c; return 0; }
NVN(nvnCommandBufferEndRecording, NVNcommandHandle, NVNcommandBuffer *c) {
    cmd_t *o = CB(c);
    uint8_t *p = cmd_alloc_keep(o, 8, OP_KEEP_END); put_hdr(p, OP_END, 8);
    o->recording = 0;
    return (NVNcommandHandle)o->begin;
}
NVN(nvnCommandBufferIsRecording, NVNboolean, const NVNcommandBuffer *c) { return ((const cmd_t *)c)->recording != 0; }
// CallCommands records a call: the called list's commands are read where they are when it runs (the SDK: command data called directly, it must
// stay until no list referring to it is in use). TotK main 0x2d4058
NVN(nvnCommandBufferCallCommands, void, NVNcommandBuffer *c, int n, const NVNcommandHandle *h) {
    for (int i = 0; i < n; i++) { uint8_t *p = cmd_alloc(CB(c), 16); put_hdr(p, OP_CALL, 16); memcpy(p + 8, &h[i], 8); }
}
// CopyCommands copies (plan item 64b; the SDK copies the command and control data, and passes Call and Copy different flags to one helper, which
// dr3-mobile read in the shipped SDK; ours recorded a call for both, nxnative's way): the records of each source, nested calls inlined, its BEGIN,
// JUMPs and END left out, are collected first (the destination's memory callback may hand out the source's memory), then recorded into the
// destination, which no longer needs the sources. Bounded as dr3's: 16 call levels, 1,000,000 records, 64 MiB; a source over a bound, with a call
// cycle or a record of a bad size is refused whole (a red line), the destination left as it was
static int copy_collect(const uint8_t *p, int depth, uint8_t **buf, size_t *len, size_t *cap, uint32_t *nrec, const uint8_t **stack) {
    if (depth > 16 || !p) return 0;
    for (int i = 0; i < depth; i++) if (stack[i] == p) return 0;
    stack[depth] = p;
    for (;;) {
        op_hdr x; memcpy(&x, p, sizeof x);
        if (x.size < 8 || (x.size & 7) || x.size > (1u << 24)) return 0;
        if (x.op == OP_END) return 1;
        if (x.op == OP_JUMP) { const uint8_t *t; memcpy(&t, p + 8, 8); if (!t) return 0; p = t; continue; }
        if (x.op == OP_CALL) { uint64_t t; memcpy(&t, p + 8, 8); if (!copy_collect((const uint8_t *)(uintptr_t)t, depth + 1, buf, len, cap, nrec, stack)) return 0; }
        else if (x.op != OP_BEGIN) {
            if (++*nrec > 1000000 || *len + x.size > (64u << 20)) return 0;
            if (*len + x.size > *cap) { *cap = (*len + x.size) * 2; uint8_t *nb = realloc(*buf, *cap); if (!nb) return 0; *buf = nb; }
            memcpy(*buf + *len, p, x.size); *len += x.size;
        }
        p += x.size;
    }
}
NVN(nvnCommandBufferCopyCommands, void, NVNcommandBuffer *c, int n, const NVNcommandHandle *h) {
#ifdef DM_TEST_COPY_AS_CALL   // negative control of gfx/tests t_copycmd only: a call, as before
    h_nvnCommandBufferCallCommands(c, n, h); return;
#endif
    uint8_t *buf = NULL; size_t len = 0, cap = 0; uint32_t nrec = 0; const uint8_t *stack[17];
    for (int i = 0; i < n; i++)
        if (!copy_collect((const uint8_t *)(uintptr_t)h[i], 0, &buf, &len, &cap, &nrec, stack)) {
            LOG("[red] CopyCommands: list %d of %d (%#llx) has a call cycle, a bad record or is over the bounds: nothing copied", i + 1, n, (unsigned long long)h[i]); free(buf); return; }
    for (size_t o = 0; o < len;) { op_hdr x; memcpy(&x, buf + o, sizeof x); memcpy(cmd_alloc(CB(c), x.size), buf + o, x.size); o += x.size; }
    free(buf);
}
// A fence in a command buffer (plan item 61a; it went to the red-reporting stub): the sync takes the fence of the submission that runs the record,
// set when it is replayed at submit (nvn_mtl.m OP_FENCE_SYNC), so a CPU wait right after the submit waits for that submission, and a list submitted
// twice leaves the second's. The SDK's fence is "all previous commands completed": the whole submission's is the same or later
NVN(nvnCommandBufferFenceSync, void, NVNcommandBuffer *c, NVNsync *s, NVNsyncCondition cond, int flags) {
    (void)cond; (void)flags; uint8_t *p = cmd_alloc(CB(c), 16); put_hdr(p, OP_FENCE_SYNC, 16); memcpy(p + 8, &s, 8);
}
NVN(nvnCommandBufferSignalEvent, void, NVNcommandBuffer *c, const NVNevent *e, NVNeventSignalMode m, NVNeventSignalLocation loc, int flags, uint32_t v) {
    (void)loc; (void)flags;
    uint8_t *p = cmd_alloc(CB(c), 24); put_hdr(p, OP_SIGNAL_EVENT, 24);
    const ev_t *ev = (const ev_t *)e; volatile uint32_t *val = ev->val; uint32_t mm = m;
    memcpy(p + 8, &val, 8); memcpy(p + 16, &mm, 4); memcpy(p + 20, &v, 4);
}
NVN(nvnCommandBufferResetCounter, void, NVNcommandBuffer *c, NVNcounterType t) {
    uint8_t *p = cmd_alloc(CB(c), 16); put_hdr(p, OP_RESET_COUNTER, 16); uint64_t tt = t; memcpy(p + 8, &tt, 8);
}
NVN(nvnCommandBufferReportCounter, void, NVNcommandBuffer *c, NVNcounterType t, NVNbufferAddress a) {
    uint8_t *p = cmd_alloc(CB(c), 24); put_hdr(p, OP_REPORT_COUNTER, 24); uint64_t tt = t; memcpy(p + 8, &tt, 8); memcpy(p + 16, &a, 8);
}
NVN(nvnCommandBufferCopyBufferToBuffer, void, NVNcommandBuffer *c, NVNbufferAddress src, NVNbufferAddress dst, size_t n, int flags) {
    (void)flags;
    uint8_t *p = cmd_alloc(CB(c), 32); put_hdr(p, OP_COPY_B2B, 32); uint64_t nn = n;
    memcpy(p + 8, &src, 8); memcpy(p + 16, &dst, 8); memcpy(p + 24, &nn, 8);
}
// uniform update: the data is copied into command memory at record time (NVN semantics: the caller may modify the original data after recording) and written into the buffer at submit
NVN(nvnCommandBufferUpdateUniformBuffer, void, NVNcommandBuffer *c, NVNbufferAddress buf, size_t bufsz, ptrdiff_t off, size_t n, const void *data) {
    { static int dbg = -1; if (dbg < 0) dbg = getenv("DM_NVN_UBLOG") != NULL;   // diagnostic: the raw arguments
      if (dbg) LOG("[ub] UpdateUniformBuffer buffer=%#llx size=%#zx offset=%#tx n=%#zx", (unsigned long long)buf, bufsz, off, n); }
    uint32_t sz = (uint32_t)(24 + n);
    uint8_t *p = cmd_alloc(CB(c), sz); put_hdr(p, OP_UPDATE_UB, (sz + 7) & ~7u);
    uint64_t dst = buf + off, nn = n; memcpy(p + 8, &dst, 8); memcpy(p + 16, &nn, 8); memcpy(p + 24, data, n);
}

