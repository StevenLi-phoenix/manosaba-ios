// iOS front end (implements front.h): the Metal view the game presents to, letterboxed to the game picture's aspect ratio; touches → dm_touch_set;
// game controllers; the software keyboard dialog; scene lifecycle, audio session and thermal logging. main.m builds the window and forwards the
// scene events to front_ios_focus_event.
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <signal.h>
#include "cloud.h"
#include "dm_input.h"
#include "front.h"
#include "game.h"
#include "hle.h"

int mtl_fps_target(void);   // gfx/nvn/nvn_mtl.m: 60, or 30 in the power-saving mode
static CAMetalLayer *g_layer;

// Game picture size: DM_GAME_RES=<w>x<h>, else the profile's screen size. The presenter keeps this aspect ratio and centers the picture; the game view
// below is laid out the same way, so normalized touch coordinates match the picture
static void game_size(double *w, double *h) {
    static double gw, gh;
    if (!gw) {
        unsigned ew, eh; const char *e = getenv("DM_GAME_RES");
        if (e && sscanf(e, "%ux%u", &ew, &eh) == 2 && ew && eh) { gw = ew; gh = eh; }
        else { gw = dm_game.screen_w > 0 ? dm_game.screen_w : 1280; gh = dm_game.screen_h > 0 ? dm_game.screen_h : 720; }
    }
    *w = gw; *h = gh;
}
// The largest rectangle of the game's aspect ratio centered in f (a difference under 1% fills f)
static CGRect letterbox_rect(CGRect f) {
    double gw, gh; game_size(&gw, &gh);
    if (f.size.width <= 0 || f.size.height <= 0) return f;
    double ga = gw / gh, wa = f.size.width / f.size.height;
    if (fabs(wa / ga - 1) < 0.01) return f;
    CGFloat w = f.size.width, h = f.size.height;
    if (wa > ga) w = floor(h * ga); else h = floor(w / ga);
    return CGRectMake(f.origin.x + floor((f.size.width - w) / 2), f.origin.y + floor((f.size.height - h) / 2), w, h);
}

// ---------- Game view: CAMetalLayer + touches ----------
// Touches: every finger gets the lowest free slot 0–15 on touch down and keeps it until it lifts; the whole set goes to the host in coordinates
// normalized to this view on every change (hle_touch.c scales them to the game's touch space)
@interface DmMetalView : UIView
- (void)liftAll;
@end
@implementation DmMetalView { UITouch *_slot[16]; }
+ (Class)layerClass { return CAMetalLayer.class; }
- (instancetype)initWithFrame:(CGRect)f {
    if (!(self = [super initWithFrame:f])) return nil;
    self.multipleTouchEnabled = YES;
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CAMetalLayer *l = (CAMetalLayer *)self.layer;
    l.drawableSize = CGSizeMake(self.bounds.size.width * l.contentsScale, self.bounds.size.height * l.contentsScale);
}
- (void)push {
    dm_touch pt[16]; int n = 0; CGSize b = self.bounds.size;
    if (b.width > 0 && b.height > 0)
        for (int i = 0; i < 16; i++) if (_slot[i]) {
            CGPoint p = [_slot[i] locationInView:self];
            pt[n++] = (dm_touch){(uint32_t)i, (float)fmin(fmax(p.x / b.width, 0), .99999), (float)fmin(fmax(p.y / b.height, 0), .99999)};
        }
    dm_touch_set(pt, n);
}
- (void)touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e {
    (void)e;
    for (UITouch *t in ts) for (int i = 0; i < 16; i++) if (!_slot[i]) { _slot[i] = t; break; }
    [self push];
}
- (void)touchesMoved:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)ts; (void)e; [self push]; }
- (void)lift:(NSSet<UITouch *> *)ts { for (UITouch *t in ts) for (int i = 0; i < 16; i++) if (_slot[i] == t) _slot[i] = nil; [self push]; }
- (void)touchesEnded:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self lift:ts]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { (void)e; [self lift:ts]; }
- (void)liftAll { for (int i = 0; i < 16; i++) _slot[i] = nil; dm_touch_set(NULL, 0); }
@end
static __weak DmMetalView *g_view;

// Black container that keeps the game view at the game's aspect ratio, centered
@interface DmLetterboxView : UIView
@property (nonatomic, strong) UIView *game;
@end
@implementation DmLetterboxView
- (void)layoutSubviews { [super layoutSubviews]; if (self.bounds.size.width > 0 && self.bounds.size.height > 0) self.game.frame = letterbox_rect(self.bounds); }
@end

// Frame rate hint for ProMotion screens: 60 Hz, or 30 Hz while the graphics layer is in its power-saving mode
@interface DmRateHint : NSObject
- (void)tick:(CADisplayLink *)l;
@end
@implementation DmRateHint
- (void)tick:(CADisplayLink *)l {
    static int last = -1; int f = mtl_fps_target();
    if (f != last) { last = f; l.preferredFrameRateRange = f == 30 ? CAFrameRateRangeMake(30, 30, 30) : CAFrameRateRangeMake(60, 60, 60); }
}
@end
static CADisplayLink *g_rate_link;

UIView *front_ios_view(CGRect frame, CGFloat scale) {
    DmLetterboxView *box = [[DmLetterboxView alloc] initWithFrame:frame];
    box.backgroundColor = UIColor.blackColor;
    DmMetalView *v = [[DmMetalView alloc] initWithFrame:letterbox_rect(frame)];
    box.game = v; [box addSubview:v]; g_view = v;
    g_layer = (CAMetalLayer *)v.layer;
    g_layer.device = MTLCreateSystemDefaultDevice();
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_layer.contentsScale = scale;
    g_layer.drawableSize = CGSizeMake(v.bounds.size.width * g_layer.contentsScale, v.bounds.size.height * g_layer.contentsScale);
    g_rate_link = [CADisplayLink displayLinkWithTarget:[DmRateHint new] selector:@selector(tick:)];
    g_rate_link.preferredFrameRateRange = CAFrameRateRangeMake(60, 60, 60);
    [g_rate_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    double gw, gh; game_size(&gw, &gh);
    LOG("front: window %.0fx%.0f, game picture %.0fx%.0f, game view %.0fx%.0f at (%.0f, %.0f)", frame.size.width, frame.size.height, gw, gh,
        v.frame.size.width, v.frame.size.height, v.frame.origin.x, v.frame.origin.y);
    dm_gamepad_start();
    return box;
}
void *front_metal_layer(void) { return (__bridge void *)g_layer; }

void front_run(void (*guest)(void)) {
    static char alt[65536];   // the alternate signal stack is per thread: the crash report must run even when a guest thread overflows its stack
    stack_t ss = {.ss_sp = alt, .ss_size = sizeof alt}; sigaltstack(&ss, 0);
    // This thread inherits the UIKit main thread's signal mask, which blocks SIGUSR2 / SIGALRM (measured on a device); the host's diagnostics need them
    sigset_t all; sigfillset(&all); pthread_sigmask(SIG_UNBLOCK, &all, NULL);
    guest();
}

// ---------- Presenting on the main thread ----------
static UIViewController *top_controller(void) {   // key window first; else a window of the active scene; else any window
    UIViewController *vc = nil, *alt = nil, *any = nil;
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class]) for (UIWindow *w in ((UIWindowScene *)s).windows) {
            if (!w.rootViewController) continue;
            if (w.isKeyWindow) vc = w.rootViewController;
            else if (s.activationState == UISceneActivationStateForegroundActive && !alt) alt = w.rootViewController;
            else if (!any) any = w.rootViewController;
        }
    if (!vc) vc = alt ?: any;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}

// Software keyboard (front.h): the main thread shows an alert with a text field, the calling guest thread waits for OK / Cancel. max_chars limits the
// text while typing (UTF-16 units, as swkbd counts; text still being composed by an input method is left alone).
// Test switch DM_TEST_KBD_TYPE="text" or ":cancel": 1.5 s after the dialog appears the text is filled in through the same editing path, then OK / Cancel
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    if (!g_layer) return -1;
    NSString *t = title && *title ? @(title) : @"Enter text";
    NSString *msg = [NSString stringWithFormat:@"%s%s%s", sub ? sub : "", sub && *sub && guide && *guide ? "\n" : "", guide ? guide : ""];
    NSString *init = initial ? @(initial) : @"", *ph = guide && *guide ? @(guide) : nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block int r = 0; __block NSString *text = nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *a = [UIAlertController alertControllerWithTitle:t message:msg.length ? msg : nil preferredStyle:UIAlertControllerStyleAlert];
        a.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
        __weak UIAlertController *wa = a;
        [a addTextFieldWithConfigurationHandler:^(UITextField *f) {
            f.text = init; f.placeholder = ph; f.clearButtonMode = UITextFieldViewModeWhileEditing; f.keyboardAppearance = UIKeyboardAppearanceDark;
            if (max_chars > 0) [f addAction:[UIAction actionWithHandler:^(UIAction *act) {
                UITextField *tf = (UITextField *)act.sender;
                if (tf.markedTextRange || (int)tf.text.length <= max_chars) return;
                NSRange keep = [tf.text rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, (NSUInteger)max_chars)];
                if ((int)NSMaxRange(keep) > max_chars) keep = [tf.text rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, (NSUInteger)max_chars - 1)];
                tf.text = [tf.text substringWithRange:keep];
            }] forControlEvents:UIControlEventEditingChanged];
        }];
        void (^finish)(int) = ^(int ok) { if (ok) text = wa.textFields.firstObject.text; r = ok; dispatch_semaphore_signal(done); };
        [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction *x) { (void)x; finish(0); }]];
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { (void)x; finish(1); }]];
        UIViewController *vc = top_controller();
        if (!vc) { LOG("[red] front: no view controller to show the keyboard dialog on; cancelled"); finish(0); return; }
        [vc presentViewController:a animated:YES completion:^{
            const char *tk = getenv("DM_TEST_KBD_TYPE"); if (!tk) return;
            NSString *typed = @(tk);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500000000), dispatch_get_main_queue(), ^{
                int cancel = [typed isEqualToString:@":cancel"];
                UITextField *tf = a.textFields.firstObject;
                if (!cancel) { tf.text = typed; [tf sendActionsForControlEvents:UIControlEventEditingChanged]; }
                LOG("front: DM_TEST_KBD_TYPE %s, text field now \"%s\"", cancel ? "cancel" : "typed", tf.text.UTF8String ?: "");
                [a dismissViewControllerAnimated:NO completion:^{ finish(!cancel); }];
            });
        }];
    });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (!r) return 0;
    snprintf(out, (size_t)out_cap, "%s", text.UTF8String ? text.UTF8String : "");
    return 1;
}

// A save transaction was refused (hle_fs.c, on the game thread): tell the player without blocking; the next transaction commits as usual
void front_save_failed(int err) {
    LOG("front: save write failed, errno=%d", err);
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *vc = top_controller();
        if (!vc) { LOG("[red] front: no view controller for the save failure alert"); return; }
        UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Not enough storage" message:@"The game could not save its progress." preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [vc presentViewController:a animated:YES completion:nil];
    });
}

// ---------- Lifecycle → host (hle.h: dm_focus_set, dm_audio_suspend; dm.h: dm_gfx_suspend, guest_clock_freeze_for) ----------
// Audio pauses for two independent reasons, background and an audio interruption (call, alarm); it resumes only when both are gone
static int g_audio_bg, g_audio_intr;
static void audio_apply(void) { dm_audio_suspend(g_audio_bg || g_audio_intr); }
// Scene events from main.m: 0 resign active, 1 enter background, 2 return to the foreground, 3 become active.
// In the background the guest clock is frozen too (like a Switch suspend), so in-game time does not jump by the time spent away
void front_ios_focus_event(int ev) {
    static const char *k[] = {"sceneWillResignActive", "sceneDidEnterBackground", "sceneWillEnterForeground", "sceneDidBecomeActive"};
    LOG("front: %s (guest clock %.3f s, audio callbacks %llu)", k[ev], guest_ns() / 1e9, (unsigned long long)dm_audio_render_calls());
    switch (ev) {
    case 0: [g_view liftAll]; dm_gamepad_reset(); dm_focus_set(2); break;   // held buttons / touches released in the background never report it
    case 1: dm_focus_set(3); g_audio_bg = 1; audio_apply(); dm_cloud_flush(1); guest_clock_freeze_for(1, 1); dm_gfx_suspend(1); break;
    // Back in the foreground any interruption counts as over: the system interrupts the session when the app goes to the background but does not
    // promise an "ended" on return; if the session is really still taken, starting the audio unit fails harmlessly and is retried on activation
    case 2: dm_cloud_foreground(); dm_gfx_suspend(0); guest_clock_freeze_for(1, 0); g_audio_bg = g_audio_intr = 0; audio_apply(); dm_focus_set(2); break;
    case 3: dm_focus_set(1); audio_apply(); break;
    }
}

// Called by main.m when the host starts: no auto-lock, thermal logging, audio session (Playback: sound plays with the mute switch on, as games do;
// stopping in the background and on interruptions is handled here)
void front_ios_lifecycle_install(void) {
    UIApplication.sharedApplication.idleTimerDisabled = YES;
    NSProcessInfo *pi = NSProcessInfo.processInfo;
    LOG("front: thermal state %ld, sustained performance profile %d", (long)pi.thermalState, (int)[pi hasPerformanceProfile:NSProcessPerformanceProfileSustained]);
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter; NSOperationQueue *mq = NSOperationQueue.mainQueue;
    [nc addObserverForName:NSProcessInfoThermalStateDidChangeNotification object:nil queue:mq usingBlock:^(NSNotification *n) {
        (void)n; LOG("front: thermal state %ld", (long)NSProcessInfo.processInfo.thermalState); }];
    AVAudioSession *as = AVAudioSession.sharedInstance; NSError *e = nil;
    if (![as setCategory:AVAudioSessionCategoryPlayback error:&e] || ![as setActive:YES error:&e]) LOG("[red] front: audio session setup failed: %s", e.localizedDescription.UTF8String);
    [nc addObserverForName:AVAudioSessionInterruptionNotification object:as queue:mq usingBlock:^(NSNotification *n) {
        AVAudioSessionInterruptionType t = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        if (t == AVAudioSessionInterruptionTypeBegan) { LOG("front: audio interrupted"); g_audio_intr = 1; audio_apply(); return; }
        // Ended: clear the reason whether or not the system says "should resume" (a dismissed alarm comes without it)
        g_audio_intr = 0; [AVAudioSession.sharedInstance setActive:YES error:nil]; audio_apply();
        LOG("front: audio interruption ended%s", g_audio_bg ? " (still in the background)" : "");
    }];
    // Test switch DM_TEST_BG=<start s>,<duration s>[;...]: simulated "enter background → return" through the same handlers
    const char *tb = getenv("DM_TEST_BG"); double tb_at, tb_dur;
    for (const char *tp = tb; tp && sscanf(tp, "%lf,%lf", &tb_at, &tb_dur) == 2; tp = strchr(tp, ';') ? strchr(tp, ';') + 1 : NULL) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(tb_at * 1e9)), dispatch_get_main_queue(), ^{
            LOG("front: DM_TEST_BG enter background"); front_ios_focus_event(0); front_ios_focus_event(1); });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((tb_at + tb_dur) * 1e9)), dispatch_get_main_queue(), ^{
            LOG("front: DM_TEST_BG return"); front_ios_focus_event(2); front_ios_focus_event(3); });
    }
}
