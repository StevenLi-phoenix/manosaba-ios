// Diagnostics export (plan item 106; lordran-native f5834c5's idea, MIT, the code is ours): one text file with the device, the GPU, memory, thermal
// state, the shader chain, the fps / GPU line and the DM_ switches in effect, then the last 8 MiB of each log (this session's, the previous one,
// the last interrupted one, perf.txt; host/rundiag.h), offered in the share sheet (AirDrop to the Mac, Files, Mail). Reached from the home screen
// (touch and hold the icon → Export Diagnostics; a launch that way exports before the host starts, so a game that ends at boot can still be
// reported) and from the page shown when the game data is missing. The header is gathered on the main thread; the file is written on another
// queue and takes no lock a stuck game thread could hold (the logs are read through their own descriptors, stderr is not flushed).
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <os/proc.h>
#include <sys/sysctl.h>
#include "dmsh_chain.h"
#include "game.h"
#include "rundiag.h"

int dm_log_fd(void);                       // main.m
const char *dm_diag_launch_note(void);     // main.m: what this launch found of the last session
int dm_diag_host_up(void);                 // main.m
int dm_perf_line(char *out, size_t n);     // gfx/nvn/hle_nvn.c
unsigned long long mtl_gpu_allocated_mb(void);
void plog(const char *fmt, ...);
extern char **environ;
static uint64_t g_t0; __attribute__((constructor)) static void diag_t0(void) { g_t0 = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); }   // about the launch

static NSString *diag_head(void) {
    NSMutableString *h = [NSMutableString string];
    NSDateFormatter *df = [NSDateFormatter new]; df.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    df.timeZone = [NSTimeZone timeZoneWithAbbreviation:@"UTC"]; df.dateFormat = @"yyyy-MM-dd'T'HH:mm:ss'Z'";
    NSDictionary *inf = NSBundle.mainBundle.infoDictionary; NSProcessInfo *pi = NSProcessInfo.processInfo;
    char hw[64] = "?"; size_t hn = sizeof hw; sysctlbyname("hw.machine", hw, &hn, NULL, 0);
    [h appendFormat:@"%s %s diagnostics, exported %@\n", dm_game.title, dm_game.version, [df stringFromDate:NSDate.date]];
    [h appendFormat:@"app %@ (%@), %s, %@\n", inf[@"CFBundleShortVersionString"], inf[@"CFBundleVersion"], hw, pi.operatingSystemVersionString];
#if DM_NVN_M3
    const char *build = "Metal 3 (iOS 18) build";
#else
    const char *build = "Metal 4 (iOS 26) build";
#endif
    [h appendFormat:@"%s, shader chain %s, this session up %.0f s\n", build, DMSH_CHAIN, (clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - g_t0) / 1e9];
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); BOOL m4 = NO;
    if (@available(iOS 26.0, *)) m4 = [dev supportsFamily:MTLGPUFamilyMetal4];
    [h appendFormat:@"GPU %@: Metal 4 %s, Metal 3 %s, argument buffers tier %d, BC textures %s, working set %.0f MB recommended, %.0f MB allocated by the app\n",
        dev.name, m4 ? "yes" : "no", [dev supportsFamily:MTLGPUFamilyMetal3] ? "yes" : "no", (int)dev.argumentBuffersSupport + 1,
        dev.supportsBCTextureCompression ? "yes" : "no", dev.recommendedMaxWorkingSetSize / 1048576.0, dev.currentAllocatedSize / 1048576.0];
    task_vm_info_data_t vi; mach_msg_type_number_t cnt = TASK_VM_INFO_COUNT; double fp = -1;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &cnt) == KERN_SUCCESS) fp = vi.phys_footprint / 1048576.0;
    [h appendFormat:@"memory: %.0f MB in the device, footprint %.0f MB, %.0f MB left before the limit\n", pi.physicalMemory / 1048576.0, fp, os_proc_available_memory() / 1048576.0];
    static const char *const therm[] = {"nominal", "fair", "serious", "critical"};
    NSInteger ts = pi.thermalState;
    [h appendFormat:@"thermal state %s, low power mode %s\n", ts >= 0 && ts < 4 ? therm[ts] : "?", pi.lowPowerModeEnabled ? "on" : "off"];
    if (dm_diag_host_up()) { char pl[128]; dm_perf_line(pl, sizeof pl); [h appendFormat:@"game running: %s; GPU memory %llu MB\n", pl, mtl_gpu_allocated_mb()]; }
    else [h appendString:@"game not running\n"];
    [h appendFormat:@"%s\n", dm_diag_launch_note()];
    [h appendString:@"switches:"]; int ns = 0;
    for (char **e = environ; *e; e++) if (!strncmp(*e, "DM_", 3) || !strncmp(*e, "MTL_", 4)) { [h appendFormat:@" %s", *e]; ns++; }
    [h appendString:ns ? @"\n" : @" none\n"];
    [h appendString:@"Each log below is its last 8 MiB at most. These are the app's own logs, not the system's crash reports (Settings → Privacy & Security → Analytics & Improvements → Analytics Data).\n"];
    return h;
}

void dm_diag_export(UIViewController *vc, void (^done)(void)) {
    static BOOL busy;
    if (busy || !vc || vc.presentedViewController) { if (done) done(); return; }
    busy = YES;
    NSString *head = diag_head();
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    char cur[PATH_MAX] = "boot.launch.txt", lp[PATH_MAX]; int lf = dm_log_fd();
    if (lf >= 0 && fcntl(lf, F_GETPATH, lp) == 0) { const char *b = strrchr(lp, '/'); strlcpy(cur, b ? b + 1 : lp, sizeof cur); }   // renamed when the session came to the foreground
    NSString *curName = @(cur);
    NSDateFormatter *df = [NSDateFormatter new]; df.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    df.timeZone = [NSTimeZone timeZoneWithAbbreviation:@"UTC"]; df.dateFormat = @"yyyyMMdd-HHmmss";
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"diagnostics"];
    [NSFileManager.defaultManager removeItemAtPath:dir error:nil];   // earlier exports
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s-diagnostics-%@Z.txt", dm_game.id, [df stringFromDate:NSDate.date]]];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const char *logs[] = {curName.UTF8String, "boot.previous.txt", RUNDIAG_KEPT, "perf.txt"};
        int rc = rundiag_report(path.fileSystemRepresentation, head.UTF8String, docs.fileSystemRepresentation, logs, 4, 8 << 20), err = errno;
        dispatch_async(dispatch_get_main_queue(), ^{
            busy = NO;
            if (rc || !vc.view.window || vc.presentedViewController) {
                if (rc) plog("[red] diagnostics export: could not write %s: %s", path.fileSystemRepresentation, strerror(err));
                if (done) done(); return;
            }
            plog("diagnostics export: %s", path.lastPathComponent.UTF8String);
            UIActivityViewController *a = [[UIActivityViewController alloc] initWithActivityItems:@[[NSURL fileURLWithPath:path]] applicationActivities:nil];
            a.completionWithItemsHandler = ^(UIActivityType t, BOOL ok, NSArray *items, NSError *e) { (void)t; (void)ok; (void)items; (void)e; if (done) done(); };
            a.popoverPresentationController.sourceView = vc.view;   // iPad: a popover from the middle of the screen
            a.popoverPresentationController.sourceRect = CGRectMake(CGRectGetMidX(vc.view.bounds), CGRectGetMidY(vc.view.bounds), 1, 1);
            a.popoverPresentationController.permittedArrowDirections = 0;
            [vc presentViewController:a animated:YES completion:nil];
        });
    });
}
