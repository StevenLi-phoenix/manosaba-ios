// The game's language from a device language tag (plan item 170, after Touchable's internal/runtime/common/ss_platform.c switch_language, MIT,
// written anew): iOS's per-app language (Settings > Apps > <game> > Language, offered when the app declares CFBundleLocalizations: ios/tools/
// project_lang.py) arrives as a BCP 47 tag ("zh-Hant-TW", "en-GB", "es-419", "pt-BR"); nn::oe::GetDesiredLanguage answers with one of the
// nn::settings language codes (NintendoSDK: ja, en-US, fr, de, it, es, zh-CN, ko, nl, pt, ru, zh-TW, en-GB, fr-CA, es-419, zh-Hans, zh-Hant), the
// best one the game supports. Here: the tag's language and script or region give the code; a code the game does not list (game.json "languages")
// gives the profile's own language, as an unknown tag does.
//   zh (cmn, wuu, hak, nan): the script first (Hans → zh-Hans, Hant → zh-Hant: iOS stores a per-app language with the device's region, so
//   Simplified Chinese chosen in Hong Kong is "zh-Hans-HK"; Touchable 69ddbd5, plan item 191), without one TW, HK, MO → zh-Hant, else zh-Hans;
//   yue → zh-Hant
//   en: GB, IE, AU, NZ, ZA → en-GB, else en-US        fr: CA → fr-CA, else fr        es: ES or none → es, else es-419 (Latin America)
//   pt → pt; ja ko de it nl ru as they are
#pragma once
#include <string.h>
#include <strings.h>

static inline int dm_lang_has(char *const *part, int np, const char *x) { for (int i = 1; i < np; i++) if (!strcasecmp(part[i], x)) return 1; return 0; }
static inline const char *dm_lang_from_tag(const char *tag) {
    if (!tag || !*tag) return NULL;
    char t[48]; size_t n = strlen(tag); if (n >= sizeof t) n = sizeof t - 1;
    memcpy(t, tag, n); t[n] = 0;
    for (char *p = t; *p; p++) if (*p == '_') *p = '-';
    char *part[4] = {0}; int np = 0;
    for (char *p = t; p && *p && np < 4;) { part[np++] = p; char *d = strchr(p, '-'); if (!d) break; *d = 0; p = d + 1; }
    const char *l = part[0];
#define has(x) dm_lang_has(part, np, x)
#ifdef DM_TEST_LANG_REGION_IGNORED   // negative control of t_lang only: the language alone
    np = 1;
#endif
    if (!strcasecmp(l, "zh") || !strcasecmp(l, "cmn") || !strcasecmp(l, "wuu") || !strcasecmp(l, "hak") || !strcasecmp(l, "nan")) {
#ifndef DM_TEST_LANG_REGION_FIRST   // negative control of t_lang only: the region before the script, as before
        if (has("Hans")) return "zh-Hans";
#endif
        return has("Hant") || has("TW") || has("HK") || has("MO") ? "zh-Hant" : "zh-Hans";
    }
    if (!strcasecmp(l, "yue")) return "zh-Hant";
    if (!strcasecmp(l, "en")) return has("GB") || has("IE") || has("AU") || has("NZ") || has("ZA") ? "en-GB" : "en-US";
    if (!strcasecmp(l, "fr")) return has("CA") ? "fr-CA" : "fr";
    if (!strcasecmp(l, "es")) return np == 1 || has("ES") ? "es" : "es-419";
    static const char *const same[] = {"ja", "ko", "de", "it", "nl", "ru", "pt"};
    for (size_t i = 0; i < sizeof same / sizeof *same; i++) if (!strcasecmp(l, same[i])) return same[i];
#undef has
    return NULL;
}
// The code for tag among the game's languages (space-separated), else NULL (keep the profile's language)
static inline const char *dm_lang_pick(const char *tag, const char *languages) {
    const char *c = dm_lang_from_tag(tag); if (!c || !languages) return NULL;
    size_t k = strlen(c);
    for (const char *p = languages; *p;) {
        while (*p == ' ') p++;
        size_t m = strcspn(p, " ");
        if (m == k && !strncmp(p, c, k)) return c;
        p += m;
    }
    return NULL;
}
