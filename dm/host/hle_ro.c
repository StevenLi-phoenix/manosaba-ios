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
