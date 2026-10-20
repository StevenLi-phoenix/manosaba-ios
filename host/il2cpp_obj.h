// The IL2CPP class of a managed object (plan item 141; armyja's nxnative-aa-port crash.c reads the same fields, MIT, written anew): an object
// starts with its Il2CppClass *, whose name and namespace pointers sit at +0x10 and +0x18 (il2cpp's class layout: image, gc_desc, name,
// namespaze). Memory is read through rd, which returns 0 for what it cannot read (a crash reads with vm_read: no second fault); strings are read
// 16 bytes at a time at 16-byte boundaries, so a name ending just before an unmapped page is still read. The result is "Namespace.Name", or "Name"
// without a namespace, and only when both look like IL2CPP identifiers (List`1, <>c, <Start>d__5, __StaticArrayInitTypeSize=24): a register or
// word holding anything else gives nothing. tools/memsnap.py does the same on a snapshot. Unit test host/tests/t_il2cpp_obj.c
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*il2cpp_read_fn)(uint64_t addr, void *dst, size_t n);   // 1: all n bytes read

static inline int il2cpp_str(il2cpp_read_fn rd, uint64_t a, char *out, size_t n) {   // 1: a NUL within n - 1 bytes
    size_t got = 0;
    while (got + 1 < n) {
        uint8_t b[16]; size_t k = 16 - (size_t)(a % 16);
        if (!rd(a, b, k)) return 0;
        for (size_t i = 0; i < k && got + 1 < n; i++) { out[got++] = (char)b[i]; if (!b[i]) return 1; }
        a += k;
    }
    return 0;
}
static inline int il2cpp_ident(const char *s, int empty_ok) {   // letters, digits and _`<>.=$|+-, starting with a letter, _, < or $
    if (!*s) return empty_ok;
#ifndef DM_TEST_IL2CPP_NO_CHECK   // negative control of t_il2cpp_obj only: any string is a name
    char c = s[0];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '<' || c == '$')) return 0;
    for (; *s; s++) { c = *s; if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("_`<>.=$|+-", c))) return 0; }
#endif
    return 1;
}
static inline int il2cpp_class_of(uint64_t obj, il2cpp_read_fn rd, char *out, size_t n) {
    uint64_t klass = 0, np = 0, nsp = 0; char nm[96], ns[96] = "";
    if (obj < 0x10000 || (obj & 7) || !rd(obj, &klass, 8) || klass < 0x10000 || (klass & 7)) return 0;
    if (!rd(klass + 0x10, &np, 8) || !rd(klass + 0x18, &nsp, 8) || np < 0x10000) return 0;
    if (!il2cpp_str(rd, np, nm, sizeof nm) || (nsp && !il2cpp_str(rd, nsp, ns, sizeof ns))) return 0;
    if (!il2cpp_ident(nm, 0) || !il2cpp_ident(ns, 1)) return 0;
    snprintf(out, n, "%s%s%s", ns, *ns ? "." : "", nm);
    return 1;
}
