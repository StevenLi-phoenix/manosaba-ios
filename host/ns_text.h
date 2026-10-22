// C strings to NSString as UTF-8 (plan item 213): NSString's %s reads the bytes in the system's default C string encoding (Mac Roman), so the
// fps line's "·" showed as "¬∑" on the iPhone (2026-10-22), and the Mac window's title, "<game title> — <fps line>", garbled a Chinese or Japanese
// title the same way. Bytes that are not UTF-8 come out one character each rather than as nothing. Checked by host/tests/t_ns_text.m
#pragma once
#import <Foundation/Foundation.h>
static inline NSString *dm_ns_text(const char *s) {
#ifndef DM_TEST_NS_TEXT_DEFAULT_ENCODING   // negative control of t_ns_text only: %s, as before
    NSString *r = s ? [NSString stringWithUTF8String:s] : nil;
    return r ?: [NSString stringWithCString:s ? s : "" encoding:NSISOLatin1StringEncoding];
#else
    return [NSString stringWithFormat:@"%s", s ? s : ""];
#endif
}
