# Host unit tests, seconds: make -C host/tests run (build output in build/, not committed)
B = build
GAME ?= manosaba
GEN = ../gen/$(GAME)/game_profile.c
# One per line, alphabetical
run: \
	$(B)/t_altstack \
	$(B)/t_arbiter \
	$(B)/t_audio \
	$(B)/t_audio_spsc \
	$(B)/t_audren \
	$(B)/t_audren_pace \
	$(B)/t_audren_pace_out \
	$(B)/t_audren_pace_sink \
	$(B)/t_cloudlist \
	$(B)/t_cloudlist_old \
	$(B)/t_cloudsync \
	$(B)/t_cmdchan \
	$(B)/t_cmdchan_partial \
	$(B)/t_cmdchan_noreopen \
	$(B)/t_condvar \
	$(B)/t_condvar_old \
	$(B)/t_waitsync \
	$(B)/t_coremask \
	$(B)/t_coremask_noproc \
	$(B)/t_crash_bt \
	$(B)/t_crash_bt_unguarded \
	$(B)/t_crash_stderr \
	$(B)/t_il2cpp_obj \
	$(B)/t_il2cpp_obj_nocheck \
	$(B)/t_envfile \
	$(B)/t_freeze \
	$(B)/t_freeze_none \
	$(B)/t_fs \
	$(B)/t_fs_case \
	$(B)/t_fs_index \
	$(B)/t_fs_kill \
	$(B)/t_fs_kill_inplace \
	$(B)/t_gameslot \
	$(B)/t_gate_svc \
	$(B)/t_gate_svc_rel \
	$(B)/t_guest_mem \
	$(B)/t_guest_mem_heapold \
	$(B)/t_guest_mem_physold \
	$(B)/t_hid_receipt \
	$(B)/t_hid_receipt_oneslot \
	$(B)/t_hid_script \
	$(B)/t_hid_script_nocircle \
	$(B)/t_hstack \
	$(B)/t_hstack_old \
	$(B)/t_ktrace \
	$(B)/t_loader_weak \
	$(B)/t_logstamp \
	$(B)/t_logstamp_every \
	$(B)/t_memsnap \
	$(B)/t_mods \
	$(B)/t_mods_case \
	$(B)/t_mouse_route \
	$(B)/t_mouse_route_swallow \
	$(B)/t_movie \
	$(B)/t_net \
	$(B)/t_net_bsd \
	$(B)/t_oe \
	$(B)/t_oe_noop \
	$(B)/t_movie_fps \
	$(B)/t_movie_fps_any \
	$(B)/t_options \
	$(B)/t_options_nocheck \
	$(B)/t_padmap \
	$(B)/t_quitwait \
	$(B)/t_quitwait_old \
	$(B)/t_relr \
	$(B)/t_relr_unchecked \
	$(B)/t_romfs_validate \
	$(B)/t_rundiag \
	$(B)/t_rundiag_nokeep \
	$(B)/t_rundiag_head \
	$(B)/t_sead \
	$(B)/t_sead_broken \
	$(B)/t_sleep \
	$(B)/t_stickdir \
	$(B)/t_stickdir_none \
	$(B)/t_swkbd \
	$(B)/t_sysevent \
	$(B)/t_threadcpu \
	$(B)/t_threaddump \
	$(B)/t_threaddump_suspended \
	$(B)/t_thread_exit \
	$(B)/t_thread_exit_old \
	$(B)/t_thread_leak \
	$(B)/t_time \
	$(B)/t_time_one \
	$(B)/t_touch \
	$(B)/t_touchpad \
	$(B)/t_touchpad_rehit \
	$(B)/t_touchpad_sensafter \
	$(B)/t_watchdog \
	$(B)/t_watchdog_nobg
	$(B)/t_altstack
	$(B)/t_arbiter
	$(B)/t_audio
	$(B)/t_audio_spsc
	$(B)/t_audren 2>/dev/null
	$(B)/t_audren_pace
	@status=0; $(B)/t_audren_pace_out > $(B)/t_audren_pace_out.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "no two frames under 1 ms apart *✗" $(B)/t_audren_pace_out.log; then \
	   cat $(B)/t_audren_pace_out.log; echo "t_audren_pace_out: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audren_pace_out: frames paced by the output come in pairs, as expected"
	@status=0; $(B)/t_audren_pace_sink > $(B)/t_audren_pace_sink.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "after a 2 s suspension: 9-11 pulls.*✗" $(B)/t_audren_pace_sink.log; then \
	   cat $(B)/t_audren_pace_sink.log; echo "t_audren_pace_sink: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_audren_pace_sink: a silent sink never restarted pulls in a burst after a suspension, as expected"
	$(B)/t_cloudlist 2>/dev/null
	@status=0; $(B)/t_cloudlist_old > $(B)/t_cloudlist_old.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "before the call returned *✗" $(B)/t_cloudlist_old.log; then \
	   cat $(B)/t_cloudlist_old.log; echo "t_cloudlist_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_cloudlist_old: a listing sent to the main queue from the main thread never starts, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_cloudsync 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_cmdchan 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_cmdchan_partial > $(B)/t_cmdchan_partial.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ ... and runs once, whole" $(B)/t_cmdchan_partial.log; then \
	   cat $(B)/t_cmdchan_partial.log; echo "t_cmdchan_partial: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_cmdchan_partial: running a line as far as it was written misses the line written in two parts, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_cmdchan_noreopen > $(B)/t_cmdchan_noreopen.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ the file replaced by another" $(B)/t_cmdchan_noreopen.log; then \
	   cat $(B)/t_cmdchan_noreopen.log; echo "t_cmdchan_noreopen: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_cmdchan_noreopen: following the first file for good misses the replaced file's lines, as expected"
	$(B)/t_condvar
	$(B)/t_condvar_old
	$(B)/t_waitsync
	$(B)/t_coremask 2>/dev/null
	@status=0; $(B)/t_coremask_noproc > $(B)/t_coremask_noproc.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ mask 8, outside the process's cores" $(B)/t_coremask_noproc.log; then \
	   cat $(B)/t_coremask_noproc.log; echo "t_coremask_noproc: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_coremask_noproc: a core mask outside the process's cores let through moves the thread to core 3, as expected"
	$(B)/t_crash_bt
	@status=0; $(B)/t_crash_bt_unguarded > $(B)/t_crash_bt_unguarded.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ a corrupt frame pointer" $(B)/t_crash_bt_unguarded.log; then \
	   cat $(B)/t_crash_bt_unguarded.log; echo "t_crash_bt_unguarded: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_crash_bt_unguarded: a backtrace reading frames directly faults on a corrupt frame pointer, as expected"
	$(B)/t_crash_stderr
	$(B)/t_il2cpp_obj
	@status=0; $(B)/t_il2cpp_obj_nocheck > $(B)/t_il2cpp_obj_nocheck.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '✗' $(B)/t_il2cpp_obj_nocheck.log)" -ne 2 ]; then \
	   cat $(B)/t_il2cpp_obj_nocheck.log; echo "t_il2cpp_obj_nocheck: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_il2cpp_obj_nocheck: any string taken for a class name names a binary one, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_envfile
	$(B)/t_freeze
	@status=0; $(B)/t_freeze_none > $(B)/t_freeze_none.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ freeze: one dump" $(B)/t_freeze_none.log; then \
	   cat $(B)/t_freeze_none.log; echo "t_freeze_none: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_freeze_none: without a test watchdog no freeze is dumped, as expected"
	$(B)/t_fs 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_fs_case 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_fs_index 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_fs_kill
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_fs_kill_inplace > $(B)/t_fs_kill_inplace.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ after every kill" $(B)/t_fs_kill_inplace.log; then \
	   cat $(B)/t_fs_kill_inplace.log; echo "t_fs_kill_inplace: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_fs_kill_inplace: a commit that removes the committed save before cloning in its place tears it under SIGKILL, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_gameslot 2>/dev/null
	$(B)/t_gate_svc
	@status=0; $(B)/t_gate_svc_rel > $(B)/t_gate_svc_rel.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ svc_dispatch gets the svc" $(B)/t_gate_svc_rel.log; then \
	   cat $(B)/t_gate_svc_rel.log; echo "t_gate_svc_rel: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_gate_svc_rel: an svc gate passing the offset alone gives an address in no module, as expected"
	$(B)/t_guest_mem 2>/dev/null
	@status=0; $(B)/t_guest_mem_physold > $(B)/t_guest_mem_physold.log 2>&1 || status=$$?; \
	 if [ "$$status" -eq 0 ] || ! grep -q "address / size not 4 KB aligned" $(B)/t_guest_mem_physold.log; then \
	   tail -5 $(B)/t_guest_mem_physold.log; echo "t_guest_mem_physold: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_physold: the old MapPhysicalMemory ends the program on an unaligned address, as expected"
	@status=0; $(B)/t_guest_mem_heapold > $(B)/t_guest_mem_heapold.log 2>&1 || status=$$?; \
	 if [ "$$status" -eq 0 ] || ! grep -q "FAIL SetHeapSize(1): InvalidSize" $(B)/t_guest_mem_heapold.log; then \
	   tail -5 $(B)/t_guest_mem_heapold.log; echo "t_guest_mem_heapold: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_guest_mem_heapold: the old SetHeapSize takes a 1-byte heap and the next growth faults, as expected"
	$(B)/t_hid_receipt 2>/dev/null
	@status=0; $(B)/t_hid_receipt_oneslot > $(B)/t_hid_receipt_oneslot.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "HID_RECEIPT FAIL cases=6 failures=1" $(B)/t_hid_receipt_oneslot.log; then \
	   cat $(B)/t_hid_receipt_oneslot.log; echo "t_hid_receipt_oneslot: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hid_receipt_oneslot: one slot for all styles logs every poll of a game polling two, as expected"
	$(B)/t_hid_script 2>/dev/null
	@status=0; $(B)/t_hid_script_nocircle > $(B)/t_hid_script_nocircle.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL LSL and LSU overlap" $(B)/t_hid_script_nocircle.log; then \
	   cat $(B)/t_hid_script_nocircle.log; echo "t_hid_script_nocircle: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hid_script_nocircle: sticks clamped per axis leave the circle on diagonals, as expected"
	$(B)/t_hstack 2>/dev/null
	DM_HSTACK=0 $(B)/t_hstack 2>/dev/null
	@status=0; DM_HSTACK=0 $(B)/t_hstack deep > $(B)/t_hstack_deep.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "touched the page below the guest stack" $(B)/t_hstack_deep.log; then \
	   cat $(B)/t_hstack_deep.log; echo "t_hstack deep with DM_HSTACK=0: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hstack deep with DM_HSTACK=0: host code on the guest stack faults on the page below it, as expected"
	@status=0; $(B)/t_hstack_old > $(B)/t_hstack_old.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || [ "$$(grep -c '^FAIL 5' $(B)/t_hstack_old.log)" -lt 3 ] || grep -q '^FAIL [^5]' $(B)/t_hstack_old.log; then \
	   cat $(B)/t_hstack_old.log; echo "t_hstack_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_hstack_old: the context as before misses the guest's stack in all three pauses, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_ktrace 2>/dev/null
	$(B)/t_loader_weak 2>/dev/null
	$(B)/t_logstamp
	@status=0; $(B)/t_logstamp_every > $(B)/t_logstamp_every.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ no stamp inside a line" $(B)/t_logstamp_every.log; then \
	   cat $(B)/t_logstamp_every.log; echo "t_logstamp_every: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_logstamp_every: a stamp before every write lands inside a line written in two goes, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_memsnap 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_mods 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_mods_case > $(B)/t_mods_case.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ 01_one's layer" $(B)/t_mods_case.log; then \
	   cat $(B)/t_mods_case.log; echo "t_mods_case: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mods_case: paths built with the program ID as asked break on a file system that tells cases apart, as expected"
	$(B)/t_mouse_route
	@status=0; $(B)/t_mouse_route_swallow > $(B)/t_mouse_route_swallow.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "press on the title bar .*✗" $(B)/t_mouse_route_swallow.log; then \
	   cat $(B)/t_mouse_route_swallow.log; echo "t_mouse_route_swallow: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_mouse_route_swallow: a monitor swallowing every press outside the picture takes the title bar from AppKit, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_movie 2>/dev/null
	$(B)/t_net
	@status=0; $(B)/t_net_bsd > $(B)/t_net_bsd.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "ENetDown 100 *✗" $(B)/t_net_bsd.log; then \
	   cat $(B)/t_net_bsd.log; echo "t_net_bsd: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_net_bsd: the FreeBSD errno numbers are not nn::socket's, as expected"
	$(B)/t_oe 2>/dev/null
	@status=0; $(B)/t_oe_noop > $(B)/t_oe_noop.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "FAIL inactivity time extended" $(B)/t_oe_noop.log; then \
	   cat $(B)/t_oe_noop.log; echo "t_oe_noop: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_oe_noop: the old stub never holds the frame rate, as expected"
	$(B)/t_movie_fps
	@status=0; $(B)/t_movie_fps_any > $(B)/t_movie_fps_any.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "main+0x770f04 .*✗" $(B)/t_movie_fps_any.log; then \
	   cat $(B)/t_movie_fps_any.log; echo "t_movie_fps_any: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_movie_fps_any: holding every cutscene holds the prologue's real-time one too, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_options 2>/dev/null
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_options_nocheck > $(B)/t_options_nocheck.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ c with a row" $(B)/t_options_nocheck.log; then \
	   cat $(B)/t_options_nocheck.log; echo "t_options_nocheck: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_options_nocheck: an option written without checking its originals is half applied, as expected"
	$(B)/t_padmap
	$(B)/t_quitwait
	@status=0; $(B)/t_quitwait_old > $(B)/t_quitwait_old.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "waits until the game leaves it *✗" $(B)/t_quitwait_old.log; then \
	   cat $(B)/t_quitwait_old.log; echo "t_quitwait_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_quitwait_old: a quit that waits for the save alone leaves inside the exit-request section, as expected"
	$(B)/t_relr
	@status=0; $(B)/t_relr_unchecked > $(B)/t_relr_unchecked.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ refused whole: a bitmap before any address" $(B)/t_relr_unchecked.log; then \
	   cat $(B)/t_relr_unchecked.log; echo "t_relr_unchecked: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_relr_unchecked: a RELR table applied unchecked writes wherever its words say, as expected"
	TMPDIR="$(abspath $(B))" $(B)/t_romfs_validate
	TMPDIR="$(abspath $(B))" $(B)/t_rundiag
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_rundiag_nokeep > $(B)/t_rundiag_nokeep.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ its log kept as boot.interrupted.txt" $(B)/t_rundiag_nokeep.log; then \
	   cat $(B)/t_rundiag_nokeep.log; echo "t_rundiag_nokeep: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rundiag_nokeep: an interrupted session whose log is not kept leaves nothing to report, as expected"
	@status=0; TMPDIR="$(abspath $(B))" $(B)/t_rundiag_head > $(B)/t_rundiag_head.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ ... and gives its last 8 MiB exactly" $(B)/t_rundiag_head.log; then \
	   cat $(B)/t_rundiag_head.log; echo "t_rundiag_head: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_rundiag_head: the start of a long log misses its last lines, as expected"
	$(B)/t_sead 2>/dev/null
	@status=0; $(B)/t_sead_broken > $(B)/t_sead_broken.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ an address past A's end" $(B)/t_sead_broken.log; then \
	   cat $(B)/t_sead_broken.log; echo "t_sead_broken: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_sead_broken: a heap taken without checking it reaches the address is found past its end, as expected"
	$(B)/t_sleep
	$(B)/t_stickdir
	@status=0; $(B)/t_stickdir_none > $(B)/t_stickdir_none.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "left stick fully left alone: bit 16 .*✗" $(B)/t_stickdir_none.log; then \
	   cat $(B)/t_stickdir_none.log; echo "t_stickdir_none: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_stickdir_none: sticks without direction buttons set none, as expected"
	$(B)/t_swkbd
	$(B)/t_sysevent 2>/dev/null
	$(B)/t_threadcpu 2>/dev/null
	$(B)/t_threaddump
	@status=0; $(B)/t_threaddump_suspended > $(B)/t_threaddump_suspended.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ the dump ends" $(B)/t_threaddump_suspended.log; then \
	   cat $(B)/t_threaddump_suspended.log; echo "t_threaddump_suspended: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_threaddump_suspended: printing while the thread is stopped waits forever for the lock it holds, as expected"
	$(B)/t_thread_exit
	DM_HSTACK=0 $(B)/t_thread_exit 2>/dev/null
	@status=0; DM_HSTACK=0 $(B)/t_thread_exit_old > $(B)/t_thread_exit_old.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "touched the retired guest stack" $(B)/t_thread_exit_old.log; then \
	   cat $(B)/t_thread_exit_old.log; echo "t_thread_exit_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_thread_exit_old: the pre-fix exit faults on the retired guest stack, as expected"
	$(B)/t_thread_leak
	$(B)/t_time 2>/dev/null
	@status=0; $(B)/t_time_one > $(B)/t_time_one.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ New York 2026-11-01 01:30, twice" $(B)/t_time_one.log; then \
	   cat $(B)/t_time_one.log; echo "t_time_one: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_time_one: local time tried as standard time only misses the repeated hour, as expected"
	$(B)/t_touch
	$(B)/t_touchpad 2>/dev/null
	@status=0; $(B)/t_touchpad_rehit > $(B)/t_touchpad_rehit.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ slid off A" $(B)/t_touchpad_rehit.log; then \
	   cat $(B)/t_touchpad_rehit.log; echo "t_touchpad_rehit: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_touchpad_rehit: a finger that takes the button it slides onto loses A, as expected"
	@status=0; $(B)/t_touchpad_sensafter > $(B)/t_touchpad_sensafter.log 2>/dev/null || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ sensitivity 0.5×" $(B)/t_touchpad_sensafter.log; then \
	   cat $(B)/t_touchpad_sensafter.log; echo "t_touchpad_sensafter: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_touchpad_sensafter: a sensitivity applied after saturation never lets 0.5× turn at full speed, as expected"
	$(B)/t_watchdog
	@status=0; $(B)/t_watchdog_nobg > $(B)/t_watchdog_nobg.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "✗ away" $(B)/t_watchdog_nobg.log; then \
	   cat $(B)/t_watchdog_nobg.log; echo "t_watchdog_nobg: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_watchdog_nobg: a watchdog that counts the background kills the app away from the screen, as expected"

$(GEN): ../../games/$(GAME)/game.json ../../tools/gen_game.py   # (without these the tests kept a profile from before a new field)
	$(MAKE) -C .. GAME=$(GAME) profile

# Parsers of outside input under the undefined-behavior sanitizer (plan item 111; MacNativeGames runs its parsers so in CI; ASan hangs in dyld on
# macOS 26): env.txt, the command channel, DM_KEYS scripts, mods, code options, RomFS checks, controller maps, the software keyboard's text, the run
# marker and the log stamps, each built again with UBSan (fatal) into build/ubsan; t_ubsan_on first shows the sanitizer is on and stops a run
U = $(B)/ubsan
UBSAN = -fsanitize=undefined -fno-sanitize-recover=undefined -g
UBSAN_T = t_envfile t_cmdchan t_hid_script t_mods t_options t_romfs_validate t_padmap t_swkbd t_rundiag t_logstamp
run-ubsan: $(U)/t_ubsan_on $(U)/t_ubsan_on_ub $(addprefix $(U)/,$(UBSAN_T))
	$(U)/t_ubsan_on
	@status=0; $(U)/t_ubsan_on_ub > $(U)/t_ubsan_on_ub.log 2>&1 || status=$$?; \
	 if [ "$$status" -eq 0 ] || ! grep -q "runtime error" $(U)/t_ubsan_on_ub.log; then \
	   cat $(U)/t_ubsan_on_ub.log; echo "t_ubsan_on_ub: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_ubsan_on_ub: a signed overflow stops the run with a runtime error, as expected"
	TMPDIR="$(abspath $(U))" $(U)/t_envfile
	TMPDIR="$(abspath $(U))" $(U)/t_cmdchan 2>/dev/null
	$(U)/t_hid_script 2>/dev/null
	TMPDIR="$(abspath $(U))" $(U)/t_mods 2>/dev/null
	TMPDIR="$(abspath $(U))" $(U)/t_options 2>/dev/null
	TMPDIR="$(abspath $(U))" $(U)/t_romfs_validate
	$(U)/t_padmap
	$(U)/t_swkbd
	TMPDIR="$(abspath $(U))" $(U)/t_rundiag
	$(U)/t_logstamp
	@echo "run-ubsan: $(words $(UBSAN_T)) parser tests clean under the undefined-behavior sanitizer"
$(U):
	mkdir -p $@
$(U)/t_ubsan_on: t_ubsan_on.c | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -o $@ t_ubsan_on.c
$(U)/t_ubsan_on_ub: t_ubsan_on.c | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -DDM_TEST_UB -o $@ t_ubsan_on.c
$(U)/t_envfile: t_envfile.c ../envfile.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_envfile.c
$(U)/t_cmdchan: t_cmdchan.c ../cmdchan.c ../dm.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_cmdchan.c
$(U)/t_hid_script: t_hid_script.c ../hle_hid.c ../touchpad.c ../touchpad.h ../hle.h ../dm.h ../dm_input.h ../front.h | $(U)
	clang -O1 -Wno-unused-function $(UBSAN) -I.. -o $@ t_hid_script.c ../touchpad.c
$(U)/t_mods: t_mods.c ../mods.c ../dm.h ../game.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_mods.c
$(U)/t_options: t_options.c ../options.c ../dm.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_options.c
$(U)/t_romfs_validate: t_romfs_validate.c ../romfs_validate.c | $(U)
	clang -O1 -Wall -Wno-unused-function $(UBSAN) -I.. -o $@ t_romfs_validate.c ../romfs_validate.c
$(U)/t_padmap: t_padmap.c ../gamepad_map.h ../dm_input.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_padmap.c
$(U)/t_swkbd: t_swkbd.c ../hle_swkbd.c | $(U)
	clang -O1 -Wno-unused-function $(UBSAN) -I.. -o $@ t_swkbd.c ../hle_swkbd.c
$(U)/t_rundiag: t_rundiag.c ../rundiag.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_rundiag.c
$(U)/t_logstamp: t_logstamp.c ../logstamp.h | $(U)
	clang -O1 -Wall -Werror $(UBSAN) -I.. -o $@ t_logstamp.c

$(B)/t_altstack: t_altstack.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_altstack.c t_altstack_stub.c ../crash.c
$(B)/t_audio: t_audio.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_audren_pace: t_audren_pace.c ../audren_pace.h | $(B)
	clang -O1 -Wall -I.. -o $@ t_audren_pace.c
$(B)/t_audren_pace_out: t_audren_pace.c ../audren_pace.h | $(B)
	clang -O1 -Wall -DDM_TEST_AUDREN_PACE_OUTPUT -I.. -o $@ t_audren_pace.c
$(B)/t_audren_pace_sink: t_audren_pace.c ../audren_pace.h | $(B)
	clang -O1 -Wall -DDM_TEST_SINK_NO_RESYNC -I.. -o $@ t_audren_pace.c
$(B)/t_audren: t_audren.c ../hle_audren.c ../audren_pace.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audren.c
$(B)/t_audio_spsc: t_audio_spsc.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_audio_spsc_tsan: t_audio_spsc.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -g -fsanitize=thread -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_cloudlist: t_cloudlist.m ../cloud.m ../cloudsync.c ../cloud.h ../game.c $(GEN) ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -fobjc-arc -o $@ t_cloudlist.m ../cloudsync.c ../game.c $(GEN) -framework Foundation -framework Network
$(B)/t_cloudlist_old: t_cloudlist.m ../cloud.m ../cloudsync.c ../cloud.h ../game.c $(GEN) ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_CLOUD_ALWAYS_DISPATCH -I.. -fobjc-arc -o $@ t_cloudlist.m ../cloudsync.c ../game.c $(GEN) -framework Foundation -framework Network
$(B)/t_cloudsync: t_cloudsync.c ../cloudsync.c ../cloud.h ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_cloudsync.c ../cloudsync.c
$(B)/t_cmdchan: t_cmdchan.c ../cmdchan.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_cmdchan.c
$(B)/t_cmdchan_noreopen: t_cmdchan.c ../cmdchan.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_CMD_NO_REOPEN -I.. -o $@ t_cmdchan.c
$(B)/t_cmdchan_partial: t_cmdchan.c ../cmdchan.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_CMD_PARTIAL -I.. -o $@ t_cmdchan.c
$(B)/t_freeze: t_freeze.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_freeze.c t_altstack_stub.c ../crash.c
$(B)/t_freeze_none: t_freeze.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_FREEZE_NONE -I.. -o $@ t_freeze.c t_altstack_stub.c ../crash.c
$(B)/t_threaddump: t_threaddump.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_threaddump.c t_condvar_stub.c ../hle_thread.c
$(B)/t_threaddump_suspended: t_threaddump.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_BT_PRINT_SUSPENDED -I.. -o $@ t_threaddump.c t_condvar_stub.c ../hle_thread.c
$(B)/t_il2cpp_obj: t_il2cpp_obj.c t_altstack_stub.c ../crash.c ../il2cpp_obj.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_il2cpp_obj.c t_altstack_stub.c ../crash.c
$(B)/t_il2cpp_obj_nocheck: t_il2cpp_obj.c t_altstack_stub.c ../crash.c ../il2cpp_obj.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_IL2CPP_NO_CHECK -I.. -o $@ t_il2cpp_obj.c t_altstack_stub.c ../crash.c
$(B)/t_crash_bt: t_crash_bt.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_crash_bt.c t_altstack_stub.c ../crash.c
$(B)/t_crash_bt_unguarded: t_crash_bt.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_BT_UNGUARDED -I.. -o $@ t_crash_bt.c t_altstack_stub.c ../crash.c
$(B)/t_gate_svc: t_gate_svc.c ../loader.c ../hle.c ../options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_gate_svc.c ../hle.c ../options.c
$(B)/t_gate_svc_rel: t_gate_svc.c ../loader.c ../hle.c ../options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -DDM_TEST_GATE_ORIG_REL -I.. -Wl,-dead_strip -o $@ t_gate_svc.c ../hle.c ../options.c
$(B)/t_mouse_route: t_mouse_route.c ../mouse_route.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_mouse_route.c
$(B)/t_mouse_route_swallow: t_mouse_route.c ../mouse_route.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_MOUSE_SWALLOW_TITLE -I.. -o $@ t_mouse_route.c
$(B)/t_relr: t_relr.c ../loader.c ../hle.c ../options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -I.. -Wl,-dead_strip -o $@ t_relr.c
$(B)/t_relr_unchecked: t_relr.c ../loader.c ../hle.c ../options.c ../dm.h ../hle.h | $(B)
	clang -O1 -Wno-deprecated-declarations -Wno-unused-function -DDM_TEST_RELR_UNCHECKED -I.. -Wl,-dead_strip -o $@ t_relr.c
$(B)/t_time: t_time.c ../hle_time.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_time.c -framework CoreFoundation
$(B)/t_time_one: t_time.c ../hle_time.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_TOPOSIX_ONE -I.. -o $@ t_time.c -framework CoreFoundation
$(B)/t_watchdog: t_watchdog.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_watchdog.c t_altstack_stub.c ../crash.c
$(B)/t_watchdog_nobg: t_watchdog.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_WATCH_NO_BG -I.. -o $@ t_watchdog.c t_altstack_stub.c ../crash.c
$(B)/t_crash_stderr: t_crash_stderr.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_crash_stderr.c t_altstack_stub.c ../crash.c
$(B)/t_arbiter: t_arbiter.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_arbiter.c t_condvar_stub.c ../hle_thread.c
$(B)/t_padmap: t_padmap.c ../gamepad_map.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_padmap.c
$(B)/t_sead: t_sead.c ../sead.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_sead.c
$(B)/t_sead_broken: t_sead.c ../sead.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_SEAD_BROKEN -I.. -o $@ t_sead.c
$(B)/t_sleep: t_sleep.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_sleep.c t_condvar_stub.c ../hle_thread.c
$(B)/t_envfile: t_envfile.c ../envfile.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_envfile.c
$(B)/t_gameslot: t_gameslot.c ../game_slot.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-result -I.. -o $@ t_gameslot.c ../game_slot.c
$(B)/t_ktrace: t_ktrace.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_ktrace.c t_condvar_stub.c ../hle_thread.c
$(B)/t_loader_weak: t_loader_weak.c ../loader.c ../hle.c ../options.c ../dm.h ../hle.h ../rodata_seal.h | $(B)
	clang -O1 -Wno-deprecated-declarations -I.. -Wl,-dead_strip -o $@ t_loader_weak.c ../hle.c ../options.c
$(B)/t_mods: t_mods.c ../mods.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_mods.c
$(B)/t_mods_case: t_mods.c ../mods.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_MODS_CASE -I.. -o $@ t_mods.c
$(B)/t_logstamp: t_logstamp.c ../logstamp.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_logstamp.c
$(B)/t_logstamp_every: t_logstamp.c ../logstamp.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_LOGSTAMP_EVERY_WRITE -I.. -o $@ t_logstamp.c
$(B)/t_memsnap: t_memsnap.c ../memsnap.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-result -I.. -o $@ t_memsnap.c ../memsnap.c
$(B)/t_sysevent: t_sysevent.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DT_OWN_GUEST_SYM -I.. -o $@ t_sysevent.c t_condvar_stub.c ../hle_thread.c
$(B)/t_threadcpu: t_threadcpu.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_threadcpu.c t_condvar_stub.c ../hle_thread.c
$(B)/t_waitsync: t_waitsync.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_waitsync.c t_condvar_stub.c ../hle_thread.c
$(B)/t_condvar: t_condvar.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle_thread.c
$(B)/t_condvar_old: t_condvar.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_CV_OLD_ORDER -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle_thread.c
$(B)/t_fs: t_fs.c t_fs_stub.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs.c t_fs_stub.c ../game.c $(GEN)
$(B)/t_fs_kill: t_fs_kill.c t_fs_stub.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_fs_kill.c t_fs_stub.c ../game.c $(GEN)
$(B)/t_fs_kill_inplace: t_fs_kill.c t_fs_stub.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_FS_COMMIT_INPLACE -I.. -o $@ t_fs_kill.c t_fs_stub.c ../game.c $(GEN)
$(B)/t_fs_index: t_fs_index.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_index.c ../game.c $(GEN)
$(B)/t_fs_case: t_fs_case.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_case.c ../game.c $(GEN)
$(B)/t_guest_mem: t_guest_mem.c ../hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_guest_mem.c ../game.c $(GEN)
$(B)/t_guest_mem_heapold: t_guest_mem.c ../hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HEAP_OLD -I.. -o $@ t_guest_mem.c ../game.c $(GEN)
$(B)/t_guest_mem_physold: t_guest_mem.c ../hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_PHYS_OLD -I.. -o $@ t_guest_mem.c ../game.c $(GEN)
$(B)/t_coremask: t_coremask.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_coremask.c t_condvar_stub.c ../hle_thread.c
$(B)/t_coremask_noproc: t_coremask.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_CORE_NO_PROCMASK -I.. -o $@ t_coremask.c t_condvar_stub.c ../hle_thread.c
$(B)/t_hid_receipt: t_hid_receipt.c ../hle_hid.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_hid_receipt.c
$(B)/t_hid_receipt_oneslot: t_hid_receipt.c ../hle_hid.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_RECEIPT_ONE_SLOT -I.. -o $@ t_hid_receipt.c
$(B)/t_hid_script: t_hid_script.c ../hle_hid.c ../touchpad.c ../touchpad.h ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_hid_script.c ../touchpad.c
$(B)/t_hid_script_nocircle: t_hid_script.c ../hle_hid.c ../touchpad.c ../touchpad.h ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HID_NO_CIRCLE -I.. -o $@ t_hid_script.c ../touchpad.c
# VP9 movies need libvpx (brew install libvpx), as the host build does (host/Makefile VPX)
VPX ?= /opt/homebrew/opt/libvpx
$(B)/t_movie: t_movie.c ../hle_movie.c ../hstack.S ../hstack.c ../movie_av.m ../movie_av.h ../movie_webm.c ../movie_webm.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -isystem $(VPX)/include -fobjc-arc -o $@ t_movie.c ../movie_av.m ../movie_webm.c ../hstack.S ../hstack.c $(VPX)/lib/libvpx.a \
		-framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo
$(B)/t_touchpad: t_touchpad.c ../touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_touchpad.c
$(B)/t_touchpad_rehit: t_touchpad.c ../touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_TP_REHIT -I.. -o $@ t_touchpad.c
$(B)/t_touchpad_sensafter: t_touchpad.c ../touchpad.c ../touchpad.h ../dm.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_TP_SENS_AFTER -I.. -o $@ t_touchpad.c
$(B)/t_options: t_options.c ../options.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_options.c
$(B)/t_options_nocheck: t_options.c ../options.c ../dm.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_OPTIONS_NO_CHECK -I.. -o $@ t_options.c
$(B)/t_net: t_net.c ../hle_net.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_net.c
$(B)/t_net_bsd: t_net.c ../hle_net.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_NET_BSD_ERRNO -I.. -o $@ t_net.c
$(B)/t_oe: t_oe.c ../hle_oe.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_oe.c ../game.c $(GEN)
$(B)/t_oe_noop: t_oe.c ../hle_oe.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_INACTIVITY_NOOP -I.. -o $@ t_oe.c ../game.c $(GEN)
$(B)/t_movie_fps: ../../games/totk/tests/t_movie_fps.c ../../games/totk/movie_fps.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ ../../games/totk/tests/t_movie_fps.c
$(B)/t_movie_fps_any: ../../games/totk/tests/t_movie_fps.c ../../games/totk/movie_fps.c ../dm.h ../game.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_MOVIE_ANY_CUTSCENE -I.. -o $@ ../../games/totk/tests/t_movie_fps.c
$(B)/t_quitwait: t_quitwait.c ../hle_lifecycle.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_quitwait.c
$(B)/t_quitwait_old: t_quitwait.c ../hle_lifecycle.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -DDM_TEST_QUIT_NO_SECTION -I.. -o $@ t_quitwait.c
$(B)/t_romfs_validate: t_romfs_validate.c ../romfs_validate.c ../romfs_validate.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_romfs_validate.c ../romfs_validate.c
$(B)/t_rundiag: t_rundiag.c ../rundiag.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_rundiag.c
$(B)/t_rundiag_nokeep: t_rundiag.c ../rundiag.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_RUNDIAG_NO_KEEP -I.. -o $@ t_rundiag.c
$(B)/t_rundiag_head: t_rundiag.c ../rundiag.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_RUNDIAG_HEAD -I.. -o $@ t_rundiag.c
$(B)/t_stickdir: t_stickdir.c ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_stickdir.c
$(B)/t_stickdir_none: t_stickdir.c ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -DDM_TEST_STICK_NO_DIRS -I.. -o $@ t_stickdir.c
$(B)/t_swkbd: t_swkbd.c ../hle_swkbd.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_swkbd.c ../hle_swkbd.c
T_THREAD_EXIT_SRC = t_thread_exit.c t_thread_exit_guest.S ../hle_thread.c ../thread.c ../entry.S ../hstack.S ../hstack.c
# The pre-fix exit only reaches the guest stack with host calls on it: t_thread_exit_old runs with DM_HSTACK=0
T_HSTACK_SRC = t_hstack.c t_hstack_guest.S ../hle_thread.c ../thread.c ../entry.S ../hstack.S ../hstack.c ../hle_table.c
$(B)/t_hstack: $(T_HSTACK_SRC) ../dm.h | $(B)
	clang -O1 -g -Wall -Wno-unused-function -I.. -o $@ $(T_HSTACK_SRC)
$(B)/t_hstack_old: $(T_HSTACK_SRC) ../dm.h | $(B)
	clang -O1 -g -Wall -Wno-unused-function -DDM_TEST_HSTACK_OLD_CONTEXT -I.. -o $@ $(T_HSTACK_SRC)
$(B)/t_thread_exit: $(T_THREAD_EXIT_SRC) ../dm.h | $(B)
	clang -O1 -g -Wall -Wno-unused-function -I.. -o $@ $(T_THREAD_EXIT_SRC)
$(B)/t_thread_exit_old: $(T_THREAD_EXIT_SRC) ../dm.h | $(B)
	clang -O1 -g -Wall -Wno-unused-function -DDM_TEST_EXIT_ON_GUEST_STACK -I.. -o $@ $(T_THREAD_EXIT_SRC)
$(B)/t_thread_leak: t_thread_leak.c ../thread.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_thread_leak.c
$(B)/t_touch: t_touch.c ../hle_touch.c ../front.h ../dm.h ../game.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_touch.c ../hle_touch.c

$(B):
	mkdir -p $@
tsan: $(B)/t_audio_spsc_tsan
	$(B)/t_audio_spsc_tsan
.PHONY: run tsan
