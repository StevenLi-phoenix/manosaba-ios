// host/ns_text.h dm_ns_text (plan item 213): the fps line with its "·" and a game title in Chinese or Japanese come back as written, and bytes that
// are not UTF-8 still give a string. %s with the default C string encoding (-DDM_TEST_NS_TEXT_DEFAULT_ENCODING): 2 fail.
#include "../ns_text.h"
#include <stdio.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-80s %s\n", what, ok ? "✓" : "✗"); }
int main(void) { @autoreleasepool {
    const char *line = "60 fps \xc2\xb7 on screen 60 \xc2\xb7 GPU 11.4 ms";   // nvn_perfline.h's separators, U+00B7
    check("the fps line's middle dots come back as U+00B7", [dm_ns_text(line) isEqualToString:@"60 fps · on screen 60 · GPU 11.4 ms"]);
    check("a game title in Chinese and Japanese comes back as written", [dm_ns_text("薩爾達傳說 王國之淚") isEqualToString:@"薩爾達傳說 王國之淚"] && [dm_ns_text("魔法少女ノ魔女裁判") isEqualToString:@"魔法少女ノ魔女裁判"]);
    check("bytes that are not UTF-8 still give a string, one character each", dm_ns_text("a\xff" "b").length == 3);
    check("NULL gives an empty string", dm_ns_text(NULL).length == 0);
    printf("== %d checks, %d failed ==\n", n, fail); return fail != 0;
} }
