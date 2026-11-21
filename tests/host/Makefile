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
FS  = $(P)/fs/framfs.cpp
K   = $(P)/kernel/shell.cpp $(P)/kernel/jobs.cpp $(P)/kernel/fs.cpp $(P)/kernel/prog.cpp
SIM = fakefram.cpp $(K) $(P)/dev/fram.cpp $(FS)
pr  = $(foreach n,$(1),$(P)/prog/$(n).cpp)

# filesystem and hour-counter tests (warnings on: these are clean)
UNIT = fstest hourstest fscktest mvcut
# whole-shell simulations (-w: the fakes are quick and dirty)
SIMS = sim quotesim cronsim catsim cpsim remsim hexsim rmsim fscksim helpsim progtest
ALL  = $(UNIT) $(SIMS)

all: $(ALL)

fstest:    fstest.cpp    $(FS) fakefram.cpp
hourstest: hourstest.cpp $(P)/fs/hours.cpp $(FS) fakefram2.cpp
fscktest:  fscktest.cpp  $(FS) fakefram2.cpp
mvcut:     mvcut.cpp     $(FS) fakefram.cpp
$(UNIT):
	$(CXX) -Wall -I fake -I $(P) -o $@ $^

sim:      sim.cpp      $(SIM) $(call pr,cat echo ls grep wc headtail tee rm mkfs df)
quotesim: quotesim.cpp $(SIM) $(call pr,cat echo ls mkfs grep wc)
cronsim:  cronsim.cpp  $(SIM) $(P)/dev/cron.cpp $(P)/dev/cons.cpp $(call pr,cat echo ls mkfs)
catsim:   catsim.cpp   $(SIM) $(call pr,cat echo ls mkfs)
cpsim:    cpsim.cpp    $(SIM) $(call pr,cat echo ls mkfs cpmv df)
remsim:   remsim.cpp   $(SIM) $(call pr,cat echo ls mkfs remember toggle)
hexsim:   hexsim.cpp   $(SIM) $(call pr,cat echo ls hexdump mkfs wc)
rmsim:    rmsim.cpp    $(SIM) $(call pr,cat echo ls hexdump mkfs rm df)
fscksim:  fscksim.cpp  $(SIM) $(call pr,cat echo ls mkfs fsck df)
helpsim:  helpsim.cpp  $(K)
progtest: progtest.cpp $(call pr,wc grep headtail)
$(SIMS):
	$(CXX) -w -I fake -I $(P) -DPLAN2040_BOARD='"cpu_rp2040"' -o $@ $^

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
	rm -f $(ALL) *.out

.PHONY: all check expected clean
