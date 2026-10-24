# WinNewPad - MinGW-w64 Makefile
#
#   make            Build both 32-bit (x86) and 64-bit (x64) executables
#   make x86        Build the 32-bit executable  -> build/x86/WinNewPad.exe
#   make x64        Build the 64-bit executable  -> build/x64/WinNewPad.exe
#   make clean      Remove build output
#
# Toolchain prefixes can be overridden, e.g. on Windows with MSYS2:
#   make x64 PREFIX64=
#   make x86 PREFIX32=

PREFIX32 ?= i686-w64-mingw32-
PREFIX64 ?= x86_64-w64-mingw32-

CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -municode -DUNICODE -D_UNICODE
LDFLAGS += -municode -mwindows -s
LDLIBS  := -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32

SRCDIR  := src
SOURCES := $(SRCDIR)/main.c
HEADERS := $(SRCDIR)/resource.h
RCFILE  := $(SRCDIR)/WinNewPad.rc
RCDEPS  := $(RCFILE) $(SRCDIR)/resource.h $(SRCDIR)/WinNewPad.manifest

.PHONY: all x86 x64 clean

all: x86 x64

x86: build/x86/WinNewPad.exe
x64: build/x64/WinNewPad.exe

build/x86/WinNewPad.exe: $(SOURCES) $(HEADERS) build/x86/WinNewPad.res | build/x86
	$(PREFIX32)gcc $(CFLAGS) $(SOURCES) build/x86/WinNewPad.res -o $@ $(LDFLAGS) $(LDLIBS)

build/x64/WinNewPad.exe: $(SOURCES) $(HEADERS) build/x64/WinNewPad.res | build/x64
	$(PREFIX64)gcc $(CFLAGS) $(SOURCES) build/x64/WinNewPad.res -o $@ $(LDFLAGS) $(LDLIBS)

build/x86/WinNewPad.res: $(RCDEPS) | build/x86
	$(PREFIX32)windres --target=pe-i386 -I$(SRCDIR) -O coff $(RCFILE) -o $@

build/x64/WinNewPad.res: $(RCDEPS) | build/x64
	$(PREFIX64)windres --target=pe-x86-64 -I$(SRCDIR) -O coff $(RCFILE) -o $@

build/x86 build/x64:
	mkdir -p $@

clean:
	rm -rf build
