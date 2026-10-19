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
	$(B)/t_cloudsync \
	$(B)/t_condvar \
	$(B)/t_condvar_old \
	$(B)/t_waitsync \
	$(B)/t_crash_stderr \
	$(B)/t_fs \
	$(B)/t_fs_case \
	$(B)/t_fs_index \
	$(B)/t_guest_mem \
	$(B)/t_hid_receipt \
	$(B)/t_hid_script \
	$(B)/t_movie \
	$(B)/t_oe \
	$(B)/t_padmap \
	$(B)/t_romfs_validate \
	$(B)/t_sleep \
	$(B)/t_swkbd \
	$(B)/t_thread_exit \
	$(B)/t_thread_exit_old \
	$(B)/t_thread_leak \
	$(B)/t_touch
	$(B)/t_altstack
	$(B)/t_arbiter
	$(B)/t_audio
	$(B)/t_audio_spsc
	TMPDIR="$(abspath $(B))" $(B)/t_cloudsync 2>/dev/null
	$(B)/t_condvar
	$(B)/t_condvar_old
	$(B)/t_waitsync
	$(B)/t_crash_stderr
	$(B)/t_fs 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_fs_case 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_fs_index 2>/dev/null
	$(B)/t_guest_mem 2>/dev/null
	$(B)/t_hid_receipt 2>/dev/null
	$(B)/t_hid_script 2>/dev/null
	TMPDIR="$(abspath $(B))" $(B)/t_movie 2>/dev/null
	$(B)/t_oe 2>/dev/null
	$(B)/t_padmap
	TMPDIR="$(abspath $(B))" $(B)/t_romfs_validate
	$(B)/t_sleep
	$(B)/t_swkbd
	$(B)/t_thread_exit
	@status=0; $(B)/t_thread_exit_old > $(B)/t_thread_exit_old.log 2>&1 || status=$$?; \
	 if [ "$$status" -ne 1 ] || ! grep -q "touched the retired guest stack" $(B)/t_thread_exit_old.log; then \
	   cat $(B)/t_thread_exit_old.log; echo "t_thread_exit_old: unexpected negative-control result ($$status)"; exit 1; \
	 fi; echo "t_thread_exit_old: the pre-fix exit faults on the retired guest stack, as expected"
	$(B)/t_thread_leak
	$(B)/t_touch

$(GEN):
	$(MAKE) -C .. GAME=$(GAME) profile

$(B)/t_altstack: t_altstack.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_altstack.c t_altstack_stub.c ../crash.c
$(B)/t_audio: t_audio.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_audio_spsc: t_audio_spsc.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_audio_spsc_tsan: t_audio_spsc.c ../hle_audio.c ../hle.h ../dm.h | $(B)
	clang -O1 -g -fsanitize=thread -Wno-unused-function -I.. -o $@ t_audio_spsc.c -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
$(B)/t_cloudsync: t_cloudsync.c ../cloudsync.c ../cloud.h ../dm.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_cloudsync.c ../cloudsync.c
$(B)/t_crash_stderr: t_crash_stderr.c t_altstack_stub.c ../crash.c ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_crash_stderr.c t_altstack_stub.c ../crash.c
$(B)/t_arbiter: t_arbiter.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_arbiter.c t_condvar_stub.c ../hle_thread.c
$(B)/t_padmap: t_padmap.c ../gamepad_map.h ../dm_input.h | $(B)
	clang -O1 -Wall -Werror -I.. -o $@ t_padmap.c
$(B)/t_sleep: t_sleep.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_sleep.c t_condvar_stub.c ../hle_thread.c
$(B)/t_waitsync: t_waitsync.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_waitsync.c t_condvar_stub.c ../hle_thread.c
$(B)/t_condvar: t_condvar.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle_thread.c
$(B)/t_condvar_old: t_condvar.c t_condvar_stub.c ../hle_thread.c ../dm.h | $(B)
	clang -O1 -Wno-unused-function -DDM_TEST_HOOKS -DDM_TEST_CV_OLD_ORDER -I.. -o $@ t_condvar.c t_condvar_stub.c ../hle_thread.c
$(B)/t_fs: t_fs.c t_fs_stub.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs.c t_fs_stub.c ../game.c $(GEN)
$(B)/t_fs_index: t_fs_index.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_index.c ../game.c $(GEN)
$(B)/t_fs_case: t_fs_case.c ../hle_fs.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_fs_case.c ../game.c $(GEN)
$(B)/t_guest_mem: t_guest_mem.c ../hle_svc.c ../rodata_seal.h ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_guest_mem.c ../game.c $(GEN)
$(B)/t_hid_receipt: t_hid_receipt.c ../hle_hid.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_hid_receipt.c
$(B)/t_hid_script: t_hid_script.c ../hle_hid.c ../hle.h ../dm.h ../dm_input.h ../front.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_hid_script.c
# VP9 movies need libvpx (brew install libvpx), as the host build does (host/Makefile VPX)
VPX ?= /opt/homebrew/opt/libvpx
$(B)/t_movie: t_movie.c ../hle_movie.c ../movie_av.m ../movie_av.h ../movie_webm.c ../movie_webm.h ../hle.h ../dm.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -isystem $(VPX)/include -fobjc-arc -o $@ t_movie.c ../movie_av.m ../movie_webm.c $(VPX)/lib/libvpx.a \
		-framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo
$(B)/t_oe: t_oe.c ../hle_oe.c ../hle.h ../dm.h ../game.c $(GEN) | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_oe.c ../game.c $(GEN)
$(B)/t_romfs_validate: t_romfs_validate.c ../romfs_validate.c ../romfs_validate.h | $(B)
	clang -O1 -Wall -Wno-unused-function -I.. -o $@ t_romfs_validate.c ../romfs_validate.c
$(B)/t_swkbd: t_swkbd.c ../hle_swkbd.c ../hle.h ../dm.h | $(B)
	clang -O1 -Wno-unused-function -I.. -o $@ t_swkbd.c ../hle_swkbd.c
T_THREAD_EXIT_SRC = t_thread_exit.c t_thread_exit_guest.S ../hle_thread.c ../thread.c ../entry.S
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
