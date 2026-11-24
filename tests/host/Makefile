# tests/host/Makefile — plan2040's PC tests
#
#   make            build every test
#   make check      build, run each, and diff its output against expected/
#   make expected   run each and (re)write expected/ (after a deliberate change:
#                   read the diff first, `git diff expected/` shows it)
#   make clean
#
# They compile the REAL sources (kernel/, fs/, dev/, prog/) with the PC's
# g++, against fakes: fake/board.h (FRAM size and layout), fake/pico/stdlib.h
# (fake clock, keyboard input from a string), fakefram*.cpp (the FRAM as an
# 8 KB array, with power cuts after N writes and read failures).

P   ?= ../..
CXX ?= g++
FS  = $(P)/fs/framfs.cpp $(P)/kernel/errstr.cpp
K   = $(P)/kernel/shell.cpp $(P)/kernel/jobs.cpp $(P)/kernel/fs.cpp $(P)/kernel/prog.cpp conf_scripts.cpp
SIM = fakefram.cpp $(K) $(P)/dev/fram.cpp $(FS)
pr  = $(foreach n,$(1),$(P)/prog/$(n).cpp)

# filesystem and hour-counter tests (warnings on: these are clean)
UNIT = fstest hourstest fscktest mvcut luttest
# whole-shell simulations (-w: the fakes are quick and dirty)
SIMS = sim quotesim cronsim catsim cpsim remsim hexsim rmsim fscksim helpsim progtest
# babywarmer app tests (the real babywarmer_v1 pin map, fake GPIO/PWM)
APP  = outtest bwsim adctest eetest drvtest
ALL  = $(UNIT) $(SIMS) $(APP)

all: $(ALL)

fstest:    fstest.cpp    $(FS) fakefram.cpp
hourstest: hourstest.cpp $(P)/fs/hours.cpp $(FS) fakefram2.cpp
fscktest:  fscktest.cpp  $(FS) fakefram2.cpp
mvcut:     mvcut.cpp     $(FS) fakefram.cpp
luttest:   luttest.cpp   $(P)/apps/babywarmer/ntclut.cpp $(P)/kernel/errstr.cpp
$(UNIT):
	$(CXX) -Wall -I fake -I $(P) -I $(P)/apps/babywarmer -o $@ $^

BW  = $(P)/apps/babywarmer
outtest:  outtest.cpp  $(P)/kernel/fs.cpp $(P)/kernel/errstr.cpp $(BW)/heater.cpp $(BW)/seg7.cpp $(BW)/dev/heater.cpp $(BW)/dev/relay.cpp $(BW)/dev/alarm.cpp $(BW)/dev/seg7.cpp
	$(CXX) -w -I fake_bw -I fake -I $(P) -I $(BW) -o $@ $^
eetest:   eetest.cpp $(BW)/dev/ntc.cpp $(BW)/ntclut.cpp $(P)/drivers/at24c.cpp $(P)/drivers/pcf8563.cpp \
          $(P)/kernel/task.cpp $(P)/kernel/fs.cpp $(P)/kernel/errstr.cpp
	$(CXX) -w -I fake_bw -I fake -I $(P) -I $(BW) -o $@ $^
adctest:  adctest.cpp $(P)/drivers/ads1115.cpp $(BW)/ntcadc.cpp ntcstub.cpp $(BW)/ntclut.cpp $(BW)/dev/skintemp.cpp $(BW)/dev/ambient.cpp \
          $(P)/kernel/task.cpp $(P)/kernel/fs.cpp $(P)/kernel/errstr.cpp
	$(CXX) -w -I fake_bw -I fake -I $(P) -I $(BW) -o $@ $^
BWSRC = $(P)/kernel/task.cpp $(P)/kernel/shell.cpp $(P)/kernel/jobs.cpp $(P)/kernel/fs.cpp $(P)/kernel/prog.cpp $(P)/kernel/errstr.cpp conf_scripts_bw.cpp \
        $(P)/dev/lamp.cpp $(P)/dev/buttons.cpp $(call pr,cat echo follow toggle adjust select) \
        $(BW)/heater.cpp $(BW)/seg7.cpp $(foreach n,heater relay alarm seg7 setpoint heaterauto percent pid phase alarmcheck guard tft,$(BW)/dev/$(n).cpp) $(BW)/safelut.cpp $(BW)/fftable.cpp \
        $(foreach n,pid phase safelut fftable alarm ledwire monitor tftwire,$(BW)/prog/$(n).cpp) $(P)/prog/label.cpp
# The TFT code is C (a blackbox copy of the st7735 repo): compiled with
# the C compiler, against the same fakes.
TFTC  = st7735 gfx icons baby heat_indicator apgar_timer text_console cue_icons display
TFTO  = $(foreach n,$(TFTC),tft_$(n).o)
tft_%.o: $(BW)/tft/%.c
	$(CC) -w -I fake_bw -I fake -c -o $@ $<
drvtest:  drvtest.cpp $(P)/drivers/fm24cl64.cpp $(P)/drivers/pcf8563.cpp $(P)/dev/rtc.cpp $(P)/dev/fram.cpp \
          $(P)/fs/framfs.cpp $(P)/kernel/fs.cpp $(P)/kernel/errstr.cpp
	$(CXX) -Wall -I fake_bw -I fake -I $(P) -o $@ $^
bwsim:    bwsim.cpp $(BWSRC) $(TFTO)
	$(CXX) -w -I fake_bw -I fake -I $(P) -I $(BW) -DPLAN2040_BOARD='"babywarmer_v1"' -o $@ $^
sim:      sim.cpp      $(SIM) $(call pr,cat echo ls grep wc headtail tee rm mkfs df)
quotesim: quotesim.cpp $(SIM) $(call pr,cat echo ls mkfs grep wc)
cronsim:  cronsim.cpp  $(SIM) $(P)/dev/cron.cpp $(P)/dev/cons.cpp $(call pr,cat echo ls mkfs)
catsim:   catsim.cpp   $(SIM) $(call pr,cat echo ls mkfs)
cpsim:    cpsim.cpp    $(SIM) $(call pr,cat echo ls mkfs cpmv df)
remsim:   remsim.cpp   $(SIM) $(call pr,cat echo ls mkfs remember toggle)
hexsim:   hexsim.cpp   $(SIM) $(call pr,cat echo ls hexdump mkfs wc)
rmsim:    rmsim.cpp    $(SIM) $(call pr,cat echo ls hexdump mkfs rm df)
fscksim:  fscksim.cpp  $(SIM) $(call pr,cat echo ls mkfs fsck df)
helpsim:  helpsim.cpp  $(K) $(P)/kernel/errstr.cpp
progtest: progtest.cpp $(call pr,wc grep headtail)
$(SIMS):
	$(CXX) -w -I fake -I $(P) -DPLAN2040_BOARD='"cpu_rp2040"' -o $@ $^

# The built-in boot script, as CMakeLists.txt builds it into conf.cpp:
# comment and empty lines left out. scripts/boot for the generic sims,
# apps/babywarmer/scripts/{boot,tftmon} for bwsim; each named after its file.
MKCONF = awk 'BEGIN { print "struct conf_script { const char *name; const char *text; };"; \
	              print "extern const conf_script conf_scripts[]; extern const int conf_nscripts;"; \
	              print "const conf_script conf_scripts[] = {" } \
	      FNR == 1 { if (n++) print "    \"\" },"; f = FILENAME; sub(/.*\//, "", f); print "  { \"" f "\", \"\"" } \
	      /^[ \t]*\#/ || /^[ \t]*$$/ { next } \
	      { gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\\n\"" } \
	      END { print "    \"\" } }; const int conf_nscripts = " n ";" }'
conf_scripts.cpp: $(P)/scripts/boot
	$(MKCONF) $< > $@
conf_scripts_bw.cpp: $(BW)/scripts/boot $(BW)/scripts/tftmon
	$(MKCONF) $^ > $@

check: $(ALL)
	@fail=0; for t in $(ALL); do \
	    ./$$t > $$t.out 2>&1; echo "exit $$?" >> $$t.out; \
	    if cmp -s $$t.out expected/$$t.out; then echo "ok    $$t"; \
	    else echo "FAIL  $$t  (diff expected/$$t.out $$t.out)"; fail=1; fi; \
	done; exit $$fail

expected: $(ALL)
	@mkdir -p expected; for t in $(ALL); do \
	    ./$$t > expected/$$t.out 2>&1; echo "exit $$?" >> expected/$$t.out; echo "wrote expected/$$t.out"; \
	done

clean:
	rm -f $(ALL) *.out *.o conf_scripts.cpp conf_scripts_bw.cpp

.PHONY: all check expected clean
