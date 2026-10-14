# Rewriter rules for manosaba (dm/tools/patch_nso.py --rules games/manosaba/rules.py), keyed by the NSO build ID of each module.
# Format: see the "Game rules" section at the top of patch_nso.py.

CUTS = {
    # main 2.0.4b (Unity 6000.0.72f1). Unity compiles the Switch player settings into a row of constant functions at the start of main (from 0x10,
    # each `mov w0, #constant; ret`). Input init 0x9d6240 builds the Input System backend 0x9d5920 with (0x50() controller styles, 0x60() controller
    # count, 0x20() touchscreen, 0x70()), and only when the touchscreen argument is 1 (0x9d5a2c) does it create the touchscreen device 0x9d5d40, whose
    # update 0x9d6070 reads nn::hid::GetTouchScreenStates<16> every frame. The game ships with 0x20 returning 0; returning 1 gives native touch
    'd81467ff90dcee10ab82759d16731cc1': [
        (0x20, 'e0031f2a', '20008052', 'player setting "touchscreen" 0x20: mov w0, wzr → mov w0, #1, so the Input System backend creates its touchscreen device'),
    ],
}
OBS = {}
PARAMS = {}
