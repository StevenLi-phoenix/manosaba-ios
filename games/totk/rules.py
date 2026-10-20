# Rewriter rules for totk (tools/patch_nso.py --rules games/totk/rules.py), keyed by the NSO build ID of each module.
# Format: see the "Game rules" section at the top of patch_nso.py.

# Tears of the Kingdom's changes are options (plan item 57: one product for 30 and 60 fps), applied at load when DM_OPTIONS or the product's
# options.default names them; with none the code is the game's as shipped (the 30 fps product):
#   fps60     the frame-rate controller's minimum interval 1, also in shrines and after a fixed interval
#   realtime  the real-time frame step (host page +0 / +4, games/totk/frame_step.c), and the cloth and hair clock from it (CAVES, host page +8)
#   scene     the scene render size from the host page +16 (DM_RENDER_RES)
#   output    the swapchain and canvas size from the host page +24 / +32 (DM_OUTPUT_RES)
CUTS = {}
OBS = {}
PARAMS = {}
HOSTREFS = {}
# Cloth and hair (plan item 163, after MacNativeGames PR #79, merged, MIT; ly05010419/MacNativeGames): the actor animation update has a second clock,
# for cloth and hair, that it builds as {seconds = factor / 30, ratio = factor} at every frame (factor: the time factor, 0 when paused, below 1 in
# slow motion), so at 60 fps it runs twice as fast and at 120 four times. Ordinary animation already gets the real-time step (realtime above). With
# realtime on, these four make it {measured seconds × factor, measured ratio × factor} (host page +0 ratio, +8 seconds; frame_step.c), the
# factor kept. Two inlined copies build it (0x246eac, 0x345904: the original fmul gives seconds from the constant 1/30, after the ratio was stored);
# two calls pass it to the out-of-line update 0x2a0e40 as s2 = factor / 30, s3 = factor (0x24944c, 0x28bc98), whose third caller (0x2afac18)
# already passes measured time and is left alone. Scratch: x8 (loaded again after both inlined sites; 0x2a0e40 loads it first) and s4 (none of
# the three callers sets v4 and 0x2a0e40 never names it: no argument). The PR makes the call sites `bl <veneer>` that ends in `b 0x2a0e40`; here
# the routine calls it and branches back (the update's return address is in the patch area: Tears of the Kingdom has no C++ exceptions to unwind).
# games/totk/tests/check_cloth_clock.py runs the four in Unicorn after the PR's table.
_HOST_X8 = ('adrp_host', 8)
CAVES = {
    '277178b7dba1b6d4949a84778d4abc58': [   # main 1.4.3
        (0x246eac, '1e200940', [_HOST_X8, 0xbd400100, 0x1e200940, 0xbd0367e0, 0xbd400900, 0x1e200940],
         'cloth clock (inlined): fmul s0, s10, s0 → ratio = page+0 × s10 stored at sp+0x364, s0 = page+8 × s10 (seconds)', 'realtime'),
        (0x345904, '1e210801', [_HOST_X8, 0xbd400101, 0x1e210801, 0xbd0117e1, 0xbd400901, 0x1e210801],
         'cloth clock (inlined): fmul s1, s0, s1 → ratio = page+0 × s0 stored at sp+0x114, s1 = page+8 × s0 (seconds)', 'realtime'),
        (0x24944c, '94015e7d', [_HOST_X8, 0xbd400104, 0x1e240842, 0x1e240863, ('bl', 0x2a0e40)],
         'cloth clock (call): s2, s3 × page+0 (the measured ratio) before bl 0x2a0e40', 'realtime'),
        (0x28bc98, '9400546a', [_HOST_X8, 0xbd400104, 0x1e240842, 0x1e240863, ('bl', 0x2a0e40)],
         'cloth clock (call): s2, s3 × page+0 (the measured ratio) before bl 0x2a0e40', 'realtime'),
        # The canvas (plan item 63, after MacNativeGames' game_patches_v143.c patch_output_res, MIT, see OPTIONS): the display configuration
        # object x19 (0x3dc9a8) keeps display 0's canvas size at +0x10, the active crop at +0x508, and lays them out again when bit 21 of +8 is set
        # (a pending output change, as at boot). Every such path reaches 0x3dcb28 with the new size in x3 (+0x10, or +0x274 of the selected
        # mode for bit 20), stores the crop at 0x3dcb2c and calls 0x3de5a8 (x3 = size, x4 = height) to rebuild the canvas targets. Here x3 and +0x10
        # become the host page's +32 (width | height << 32) when that is not 0, so the UI and every canvas target are sized to it; 0 leaves both as
        # they were (the game's own canvas, handheld's included). MacNativeGames stores its size unconditionally. Scratch: x9-x11 and the flags,
        # read by none of 0x3dcb2c-0x3dcb40 and clobbered by the call at 0x3dcb44, which every path from here reaches. 0x3dcd5c's branch to 0x3dcb2c
        # (bit 4: the crop from +0x518, scaled against the canvas) does not come through here, as in MacNativeGames.
        # games/totk/check_output_res.py runs it in Unicorn.
        (0x3dcb28, 'd360fc64', [('adrp_host', 9), 0xf940112a, 0xf9400a6b, 0xf100015f, 0x9a831143, 0x9a8b114b, 0xf9000a6b, ('orig',)],
         'canvas: lsr x4, x3, #32 → x10 = page+32; if x10: x3 = x10, canvas +0x10 = x10 (ldr x10, [x9, #32]; ldr x11, [x19, #0x10]; cmp x10, #0; '
         'csel x3, x10, x3, ne; csel x11, x10, x11, ne; str x11, [x19, #0x10]); then lsr x4, x3, #32', 'output'),
    ],
}
OPTIONS = {
    '277178b7dba1b6d4949a84778d4abc58': {   # main 1.4.3
        # 60 fps: the seven controller words of MacNativeGames' game_patches_v143.c (ly05010419/MacNativeGames, MIT, reviewed at
        # 41e1e0caf43d58c4af3c478ca2d1bafd9dba7093; they check them in Unicorn: raising, lowering, cooldown, listener veto, the slower of CPU / GPU).
        # The frame-rate controller 0x2db174 (x0 = x20 = the system object + 0x200, x1 = x19 = the system object; called from 0x2daa50 every frame)
        # turns the larger of the frame's CPU and GPU time (sys+0x120 / +0x128 ticks) into desired = trunc(time * 60 + bias) + 1, clamps it to
        # [ctl+0 minimum = 2, ctl+4 maximum = 3], raises the interval after ctl+0x10 frames over it, and lowers it to the stored candidate ctl+0x44 once
        # the cooldown ctl+0x4c is over and no listener (ctl+0x68) vetoes. It writes the interval to sys+0x88 and 60 / interval to sys+0x250, from
        # which the step is taken (0x2daa54: ratio = 60 / sys+0x250 * 0.5, 1 at 30 fps; 0x3e5638 the same before a 0.5 / 1 selector), so the game
        # keeps its speed at whatever interval is chosen. These words lower the minimum to 1 (60 fps); slow scenes still raise it to 2 or 3.
        'fps60': [
            (0x2db204, '5f01086b', '5f050071', 'frame-rate controller clamp: cmp w10, w8 (minimum) → cmp w10, #1'),
            (0x2db208, '0ab1891a', '2aa59f1a', 'frame-rate controller clamp: csel w10, w8, w9, lt → csinc w10, w9, wzr, ge (desired at least 1)'),
            (0x2db22c, '8a4640b9', '1f2003d5', 'frame-rate controller: ldr w10, [x20, #0x44] → nop (try lowering when desired < current, not only when the stored candidate is)'),
            (0x2db340, '880240b9', '28008052', 'frame-rate controller lowering: ldr w8, [x20] (minimum) → mov w8, #1'),
            (0x2db34c, 'f5030a2a', '55a59f1a', 'frame-rate controller lowering: mov w21, w10 → csinc w21, w10, wzr, ge (new interval at least 1)'),
            (0x2db360, 'bf01086b', 'bf050071', 'frame-rate controller raising: cmp w13, w8 (minimum) → cmp w13, #1'),
            (0x2db36c, 'acc1881a', 'acc59f1a', 'frame-rate controller raising: csel w12, w13, w8, gt → csinc w12, w13, wzr, gt (next lower candidate at least 1)'),
            # Shrines (nxnative PR #11, digitalhivehub 97172c1 / a6b7191 patch_nso.py, MIT; checked in Unicorn by check_frame_rate.py): entering
            # one sets bit 0 of owner+0x4f8 and owner+0x508 = 2, and the apply step 0xb00f3c then forces that fixed interval (0xb00fd0 → 0x2b3cfc0):
            # 30 fps there however light the frame. The apply step and its inlined copy (in 0x1ff60cc, which clears the bit first and so only
            # applies a racing set) now always take the "not forced" branch, so the controller decides as outside
            (0xb00f4c, '69000036', '03000014', 'shrine fixed interval: tbz w9, #0, 0xb00f58 → b 0xb00f58 (never forced)'),
            (0x1ff6104, '6a000036', '03000014', 'shrine fixed interval (inlined copy): tbz w10, #0, 0x1ff6110 → b 0x1ff6110'),
            # Leaving a fixed interval re-arms the lowering only when the minimum (ctl+0, 2 as data) is below the current interval: from 2 it
            # never tried 1 again. The helper (0x2b3d268) and its two inlined copies read the minimum: now 1
            (0x2b3d268, '0a0040b9', '2a008052', 'leaving a fixed interval: ldr w10, [x0] (minimum) → mov w10, #1'),
            (0xb00f64, '0a0042b9', '2a008052', 'leaving a fixed interval (inlined in the apply step): ldr w10, [x0, #0x200] → mov w10, #1'),
            (0x1ff6138, '0a0142b9', '2a008052', 'leaving a fixed interval (inlined in 0x1ff60cc): ldr w10, [x8, #0x200] → mov w10, #1'),
        ],
        # Real-time frame step: method from MacNativeGames' game_patches_v143.c (patch_realtime; MIT, see above): the two producers of the step take
        # the measured frame time from a page the host writes at every present (games/totk/frame_step.c), so the game keeps its speed at any frame rate
        # (90 or 120 fps, or a frame that misses its interval). Host page: +0 ratio = seconds × 30, +4 ticks = seconds × 60, one 64-bit store.
        #  - 0x2daa54..0x2daa7c, after the controller (0x2daa50): ratio = 60 / (float)sys+0x250 * 0.5, stored to 0x3a22128. Now s0 = ratio read at
        #    0x2daa58; x9 is free (reloaded at 0x2daa68), s1 is dead after the store (next read is after the bl at 0x2daa90). As in MacNativeGames.
        #  - 0x3e562c..0x3e5658: s8 = 60 / (float)sys+0x250 × (sys+0x1da bit 0 ? 0.5 : 1). Now s0 = ticks read at 0x3e5638 and the fdiv at 0x3e564c is
        #    gone; x9 is free between 0x3e562c and 0x3e5644, where it is loaded. MacNativeGames puts a veneer at 0x3e5658 instead (load, fmul, branch
        #    back); this does the same in place.
        'realtime': [
            (0x2daa54, 'bd425260', ('adrp', 9), 'step ratio: ldr s0, [x19, #0x250] → adrp x9, host page'),
            (0x2daa58, '52a84e08', ('ldr_s', 0, 9, 0), 'step ratio: mov w8, #0x42700000 → ldr s0, [x9] (host page +0: ratio)'),
            (0x2daa6c, '00d8215e', '1f2003d5', 'step ratio: scvtf s0, s0 → nop (s0 is read from the host page)'),
            (0x2daa70, '2018201e', '1f2003d5', 'step ratio: fdiv s0, s1, s0 → nop'),
            (0x2daa74, '01102c1e', '1f2003d5', 'step ratio: fmov s1, #0.5 → nop'),
            (0x2daa78, '0008211e', '1f2003d5', 'step ratio: fmul s0, s0, s1 → nop (ratio stored as read)'),
            (0x3e562c, 'bd4252c0', ('adrp', 9), 'step ticks: ldr s0, [x22, #0x250] → adrp x9, host page'),
            (0x3e5638, '5e21d800', ('ldr_s', 0, 9, 4), 'step ticks: scvtf s0, s0 → ldr s0, [x9, #4] (host page +4: ticks)'),
            (0x3e564c, '2018201e', '1f2003d5', 'step ticks: fdiv s0, s1, s0 → nop (s0 is read from the host page)'),
        ],
        # Scene render size (plan item 52, after MacNativeGames PR #19, merged 2026-10-19, MIT): 0x585da0 reads the scene size {w11, w10} from the
        # graphics parameter (ldp at 0x585e38: docked 1600x900, handheld 1280x720 per the PR) and stores it as floats at renderer +0xcc4 / +0xcc8, from
        # which every scene render target is sized; 0x585e64 converts w9 (the output size) to +0xcc0, so w9 / x9 stay. Every path reaches 0x585e48.
        # Now x17 = host page (0x585e48), s0 / s1 = page +16 / +20 (0x585e50), stored at 0x585e54 / 0x585e60. x17 instead of the PR's x3: the
        # function (0x585da0-0x586000, 152 instructions) uses neither, and x17 is no argument register for the calls after it. The host writes the
        # page (games/totk/frame_step.c).
        'scene': [
            (0x585e48, '1e220160', ('adrp', 17), 'scene size: scvtf s0, w11 → adrp x17, host page'),
            (0x585e50, '60c60cbd', '2006422d', 'scene size: str s0, [x19, #0xcc4] → ldp s0, s1, [x17, #16] (host page +16: width, height)'),
            (0x585e54, '4001221e', '60c60cbd', 'scene size: scvtf s0, w10 → str s0, [x19, #0xcc4] (width)'),
            (0x585e60, '60ca0cbd', '61ca0cbd', 'scene size: str s0, [x19, #0xcc8] → str s1, [x19, #0xcc8] (height)'),
        ],
        # Output size (plan item 63, "native 2K": after MacNativeGames' game_patches_v143.c patch_output_res, ly05010419/MacNativeGames, MIT, reviewed
        # at 41e1e0caf43d58c4af3c478ca2d1bafd9dba7093). The game composes its frame (the UI, the scene's final upscale) in a canvas the size of
        # its output, 1920x1080 docked, and presents the swapchain's crop of it. Two parts: the swapchain size, a constant built at 0xbdb578 /
        # 0xbdb584 and stored at 0xbdb588 (stur x8, [x20, #0x14]), from the host page +24 here (x8 is loaded again at 0xbdb58c; the adrp / add
        # of x9 between stay), and the canvas, a code cave at 0x3dcb28 (CAVES above). MacNativeGames writes the size into the two movs at load;
        # their note: changing only the crop leaves every UI and composition target at 1080p. The host writes the page (games/totk/frame_step.c):
        # +24 the swapchain, 1920x1080 unless DM_OUTPUT_RES; +32 the canvas, 0 (the game's own) unless DM_OUTPUT_RES. The scene's render size
        # is the option scene's; above 1920x1080 the canvas targets need the heap overlay of games/totk/heapdef.py.
        'output': [
            (0xbdb578, 'd280f008', ('adrp', 8), 'swapchain size: mov x8, #0x780 → adrp x8, host page'),
            (0xbdb584, '0887c0f2', '080d40f9', 'swapchain size: movk x8, #0x438, lsl #32 → ldr x8, [x8, #24] (host page +24: width | height << 32)'),
        ],
    },
}
