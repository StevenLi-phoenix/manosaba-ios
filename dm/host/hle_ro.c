// nn::ro（动态载入 NRO 模块）：真机经 ldr:ro 服务映射 NRO。宿主只跑离线改写过、编进程序的模块，运行期载不了新代码。
// Unity 在启动时读 rom:/.nrr/dynamic.nrr 注册可载入的原生插件；游戏没带插件时文件不存在、什么也不载。
// LookupSymbol 按名字在已装载的模块（main / sdk / subsdk*）里找，与 dlsym 同义；真要 LoadModule 就响亮失败。
// nn::ro (dynamically loaded NRO modules): on the real device the ldr:ro service maps NROs. The host only runs modules rewritten offline and built into the program, so no new code can be loaded at run time.
// Unity reads rom:/.nrr/dynamic.nrr at startup to register loadable native plugins; when the game ships no plugin the file is absent and nothing is loaded.
// LookupSymbol searches the loaded modules (main / sdk / subsdk*) by name, like dlsym; an actual LoadModule fails loudly.
#include "hle.h"

HLE_FORCE_OPT("_ZN2nn2ro10InitializeEv", "ldr:ro 连接：宿主不连服务", Result, h_ro_Init, void) { return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2ro18RegisterModuleInfoEPNS0_16RegistrationInfoEPKv", "NRR 登记：宿主不校验 NRO 哈希", Result, h_ro_RegInfo, void *info, const void *nrr) { (void)info; (void)nrr; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2ro20UnregisterModuleInfoEPNS0_16RegistrationInfoE", "NRR 注销：同上", Result, h_ro_UnregInfo, void *info) { (void)info; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2ro13GetBufferSizeEPmPKv", "NRO 所需缓冲：宿主不载 NRO", Result, h_ro_BufSize, size_t *out, const void *nro) { (void)nro; *out = 0; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2ro10LoadModuleEPNS0_6ModuleEPKvPvmi", "载入 NRO：运行期新代码须先离线改写，宿主不支持（响亮失败）", Result, h_ro_Load, void *mod, const void *nro, void *buf, size_t sz, int flag) {
    (void)mod; (void)buf; (void)flag; LOG("【红】nn::ro::LoadModule：游戏要在运行期载入 NRO（%p，缓冲 %#zx 字节）——宿主只跑离线改写过的模块，载入失败", nro, sz);
    return RES_FAIL;
}
HLE_FORCE_OPT("_ZN2nn2ro12UnloadModuleEPNS0_6ModuleE", "卸载 NRO：从未载入过", Result, h_ro_Unload, void *mod) { (void)mod; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn2ro12ProtectRelroEPKvS2_S2_S2_S2_", "RELRO 保护：宿主装载器自己设段权限", Result, h_ro_Relro, const void *a, const void *b, const void *c, const void *d, const void *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2ro12LookupSymbolEPmPKc", "按名找符号：在已装载模块里找（同 dlsym）", Result, h_ro_Lookup, uint64_t *out, const char *name) {
    void *p = guest_sym(name);
    if (g_flag.imports) LOG("nn::ro::LookupSymbol %s → %p", name, p);
    if (!p) return RES_FAIL;
    *out = (uint64_t)p; return RES_OK;
}
HLE_FORCE_OPT("_ZN2nn2ro18LookupModuleSymbolEPmPKNS0_6ModuleEPKc", "在指定 NRO 里找符号：宿主没载过 NRO", Result, h_ro_LookupMod, uint64_t *out, const void *mod, const char *name) {
    (void)out; (void)mod; LOG("nn::ro::LookupModuleSymbol %s：没有载入的 NRO", name); return RES_FAIL;
}

// C++ 例外回溯（sdk 的 libunwind）按 pc 取所在模块的 eh_frame_hdr：真机经 svc QueryMemory 找出 pc 所在的代码块与其后的数据块，
// 再读模块的 MOD0 头。宿主模块不在 Horizon 内存块里，sdk 的实现会中止；这里直接按已装载模块给出同样的结果。
// ExceptionInfo（sdk+0x15a850 写、libunwind sdk+0x668e64 读）= { uint64 模块首址; uint64 模块大小; uint64 eh_frame_hdr 相对首址偏移; uint64 eh_frame_hdr 字节数 }，
// eh_frame_hdr 起止 = MOD0 + MOD0[+0x10] / MOD0 + MOD0[+0x14]（有符号 32 位，相对 MOD0）。pc 不在任何模块里返回 false。
// 改写器让补丁区里的调用以原调用点 +4 为返回地址（patch_nso.py site_xbranch），回溯只经过原代码，原 FDE 照用。
// C++ exception unwinding (the sdk's libunwind) fetches the eh_frame_hdr of the module containing pc: the real device uses svc QueryMemory to find the code block containing pc and the data blocks after it,
// then reads the module's MOD0 header. Host modules are not in Horizon memory blocks, so the sdk implementation aborts; here the same result is given directly from the loaded modules.
// ExceptionInfo (written by sdk+0x15a850, read by libunwind at sdk+0x668e64) = { uint64 module start; uint64 module size; uint64 eh_frame_hdr offset from start; uint64 eh_frame_hdr bytes },
// eh_frame_hdr start / end = MOD0 + MOD0[+0x10] / MOD0 + MOD0[+0x14] (signed 32-bit, relative to MOD0). Returns false when pc is in no module.
// The rewriter makes calls from the patch area return to the original call site + 4 (patch_nso.py site_xbranch), so unwinding only passes through original code and the original FDEs apply.
HLE_FORCE_OPT("_ZN2nn2ro6detail16GetExceptionInfoEPNS1_13ExceptionInfoEm", "例外回溯取 eh_frame_hdr：按宿主已装载模块的 MOD0 头", uint32_t, h_ro_ExcInfo, uint64_t *info, uint64_t pc) {
    module_t *m = module_of((const void *)pc);
    if (!m) return 0;
    const uint8_t *mod0 = m->base + *(const uint32_t *)(m->base + 4);
    if (memcmp(mod0, "MOD0", 4)) { LOG("【红】GetExceptionInfo：%s 的 MOD0 头不在原位", m->name); return 0; }
    int32_t hs = *(const int32_t *)(mod0 + 0x10), he = *(const int32_t *)(mod0 + 0x14);
    info[0] = (uint64_t)m->base; info[1] = m->size;
    info[2] = (uint64_t)(mod0 + hs - m->base); info[3] = (uint64_t)(he - hs);
    return 1;
}
