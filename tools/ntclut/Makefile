# tools/ntclut/Makefile — the NTC calibration checker, for the PC
#
#   make                      build ./ntclut
#   ./ntclut ntc.pts          what the board will make of it
#   ./ntclut -c -6521 ntc.pts the temperature for an ADC code

P   ?= ../..
CXX ?= g++

ntclut: ntclut.cpp $(P)/apps/babywarmer/ntclut.cpp $(P)/kernel/errstr.cpp $(P)/apps/babywarmer/ntclut.h
	$(CXX) -Wall -O2 -I $(P) -I $(P)/apps/babywarmer -o $@ $(filter %.cpp,$^) -lm

clean:
	rm -f ntclut

.PHONY: clean
