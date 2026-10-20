// The game's language from a device language tag (host/lang.h, plan item 170): iOS tags with script and region to nn::settings codes, then only the
// game's own languages. Negative control: -DDM_TEST_LANG_REGION_IGNORED (the language alone) fails 5.
#include "../lang.h"
#include <stdio.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-96s %s\n", what, ok ? "✓" : "✗"); }
static int is(const char *got, const char *want) { return got && want ? !strcmp(got, want) : got == want; }
int main(void) {
    check("zh-Hant-TW, zh-HK, zh-Hant, yue → zh-Hant; zh-Hans-CN, zh, zh_CN, zh-SG → zh-Hans",
          is(dm_lang_from_tag("zh-Hant-TW"), "zh-Hant") && is(dm_lang_from_tag("zh-HK"), "zh-Hant") && is(dm_lang_from_tag("zh-Hant"), "zh-Hant") && is(dm_lang_from_tag("yue"), "zh-Hant")
          && is(dm_lang_from_tag("zh-Hans-CN"), "zh-Hans") && is(dm_lang_from_tag("zh"), "zh-Hans") && is(dm_lang_from_tag("zh_CN"), "zh-Hans") && is(dm_lang_from_tag("zh-SG"), "zh-Hans"));
    check("en-GB, en-AU → en-GB; en, en-US, en-CA → en-US",
          is(dm_lang_from_tag("en-GB"), "en-GB") && is(dm_lang_from_tag("en-AU"), "en-GB") && is(dm_lang_from_tag("en"), "en-US") && is(dm_lang_from_tag("en-US"), "en-US") && is(dm_lang_from_tag("en-CA"), "en-US"));
    check("fr-CA → fr-CA, fr-FR → fr; es-ES, es → es; es-MX, es-419 → es-419",
          is(dm_lang_from_tag("fr-CA"), "fr-CA") && is(dm_lang_from_tag("fr-FR"), "fr") && is(dm_lang_from_tag("es-ES"), "es") && is(dm_lang_from_tag("es"), "es")
          && is(dm_lang_from_tag("es-MX"), "es-419") && is(dm_lang_from_tag("es-419"), "es-419"));
    check("ja-JP, ko-KR, de, it, nl, ru, pt-BR as their languages; sv-SE, empty and NULL → none",
          is(dm_lang_from_tag("ja-JP"), "ja") && is(dm_lang_from_tag("ko-KR"), "ko") && is(dm_lang_from_tag("de"), "de") && is(dm_lang_from_tag("it"), "it") && is(dm_lang_from_tag("nl"), "nl")
          && is(dm_lang_from_tag("ru"), "ru") && is(dm_lang_from_tag("pt-BR"), "pt") && !dm_lang_from_tag("sv-SE") && !dm_lang_from_tag("") && !dm_lang_from_tag(NULL));
    const char *mano = "zh-Hans zh-Hant", *totk = "ja en-US en-GB fr fr-CA de it es es-419 nl ru ko zh-Hans zh-Hant";
    check("the game's languages: zh-Hant-HK picked for manosaba; en-GB not (its profile's own then)", is(dm_lang_pick("zh-Hant-HK", mano), "zh-Hant") && !dm_lang_pick("en-GB", mano));
    check("... Tears of the Kingdom: es-MX → es-419, pt-BR not listed (none), ja-JP → ja", is(dm_lang_pick("es-MX", totk), "es-419") && !dm_lang_pick("pt-BR", totk) && is(dm_lang_pick("ja-JP", totk), "ja"));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
