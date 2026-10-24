// gfx/nvn/hle/samplers.inc, part of hle_nvn.c (included there in order, one translation unit): Samplers
// ---------- Samplers ----------
#define SB(b) (&((samp_builder_t *)(b))->p)
NVN(nvnSamplerBuilderSetDefaults, void, NVNsamplerBuilder *b) {
    NVNdevice *dev = ((samp_builder_t *)b)->dev; memset(b, 0, sizeof(samp_builder_t)); ((samp_builder_t *)b)->dev = dev;
    samp_par *o = SB(b);
    o->minf = NVN_MIN_FILTER_NEAREST_MIPMAP_LINEAR; o->magf = NVN_MAG_FILTER_LINEAR;   // GL defaults
    o->ws = o->wt = o->wr = NVN_WRAP_MODE_REPEAT; o->cmp_func = NVN_COMPARE_FUNC_LESS;
    o->lod_min = -1000.f; o->lod_max = 1000.f; o->aniso = 1.f;
}
NVN(nvnSamplerBuilderSetDevice, void, NVNsamplerBuilder *b, NVNdevice *d) { ((samp_builder_t *)b)->dev = d; }
NVN(nvnSamplerBuilderSetMinMagFilter, void, NVNsamplerBuilder *b, NVNminFilter mi, NVNmagFilter ma) { SB(b)->minf = mi; SB(b)->magf = ma; }
NVN(nvnSamplerBuilderSetWrapMode, void, NVNsamplerBuilder *b, NVNwrapMode s, NVNwrapMode t, NVNwrapMode r) { SB(b)->ws = s; SB(b)->wt = t; SB(b)->wr = r; }
NVN(nvnSamplerBuilderSetLodClamp, void, NVNsamplerBuilder *b, float lo, float hi) { SB(b)->lod_min = lo; SB(b)->lod_max = hi; }
NVN(nvnSamplerBuilderSetLodBias, void, NVNsamplerBuilder *b, float v) { SB(b)->lod_bias = v; }
NVN(nvnSamplerBuilderSetCompare, void, NVNsamplerBuilder *b, NVNcompareMode m, NVNcompareFunc f) { SB(b)->cmp_mode = m; SB(b)->cmp_func = f; }
NVN(nvnSamplerBuilderSetBorderColor, void, NVNsamplerBuilder *b, const float *c) { memcpy(SB(b)->border, c, 16); }
// Min / max reduction: MTLSamplerDescriptor.reductionMode from macOS / iOS 26 (samp_by_id; GirlNet's Persona 5 Royal branch 0293081, lordran-native
// 0a35532). Before that it averages, reported once there
NVN(nvnSamplerBuilderSetReductionFilter, void, NVNsamplerBuilder *b, NVNsamplerReduction r) {
    if ((unsigned)r > NVN_SAMPLER_REDUCTION_MAX) { LOG("[red] SamplerBuilderSetReductionFilter %d: not a reduction, averaging", (int)r); r = NVN_SAMPLER_REDUCTION_AVERAGE; }
    SB(b)->reduction = (uint8_t)r;
}
NVN(nvnSamplerBuilderSetMaxAnisotropy, void, NVNsamplerBuilder *b, float v) { SB(b)->aniso = v; }
NVN(nvnSamplerBuilderSetLodSnap, void, NVNsamplerBuilder *b, float v) { (void)b; (void)v; }   // No Metal equivalent, ignored
NVN(nvnSamplerBuilderGetMinMagFilter, void, const NVNsamplerBuilder *b, NVNminFilter *mi, NVNmagFilter *ma) {
    const samp_par *o = &((const samp_builder_t *)b)->p; *mi = (NVNminFilter)o->minf; *ma = (NVNmagFilter)o->magf;
}
NVN(nvnSamplerBuilderGetWrapMode, void, const NVNsamplerBuilder *b, NVNwrapMode *s, NVNwrapMode *t, NVNwrapMode *r) {
    const samp_par *o = &((const samp_builder_t *)b)->p; *s = (NVNwrapMode)o->ws; *t = (NVNwrapMode)o->wt; *r = (NVNwrapMode)o->wr;
}
NVN(nvnSamplerBuilderGetLodClamp, void, const NVNsamplerBuilder *b, float *lo, float *hi) { const samp_par *o = &((const samp_builder_t *)b)->p; *lo = o->lod_min; *hi = o->lod_max; }   // (plan item 64a)
NVN(nvnSamplerBuilderGetLodBias, float, const NVNsamplerBuilder *b) { return ((const samp_builder_t *)b)->p.lod_bias; }
NVN(nvnSamplerBuilderGetCompare, void, const NVNsamplerBuilder *b, NVNcompareMode *m, NVNcompareFunc *f) { const samp_par *o = &((const samp_builder_t *)b)->p; *m = (NVNcompareMode)o->cmp_mode; *f = (NVNcompareFunc)o->cmp_func; }
NVN(nvnSamplerBuilderGetBorderColor, void, const NVNsamplerBuilder *b, float *c) { memcpy(c, ((const samp_builder_t *)b)->p.border, 16); }
NVN(nvnSamplerBuilderGetMaxAnisotropy, float, const NVNsamplerBuilder *b) { return ((const samp_builder_t *)b)->p.aniso; }
NVN(nvnSamplerInitialize, NVNboolean, NVNsampler *s, const NVNsamplerBuilder *b) {
    samp_t *o = (samp_t *)s; o->magic = MAGIC('S', 'M', 'P', 0); o->p = ((const samp_builder_t *)b)->p; return 1;
}
NVN(nvnSamplerFinalize, void, NVNsampler *s) { ((samp_t *)s)->magic = 0; }

