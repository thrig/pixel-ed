CFLAGS=-O2 -std=c99 -fno-diagnostics-color -fstack-protector-strong\
	-pipe -pedantic -Wall -Wextra -fpie -Wl,-pie\
	-Wcast-qual -Wconversion -Wformat-security -Wformat=2\
	-Wno-unused-function -Wno-unused-parameter -Wnull-dereference\
	-Wpointer-arith -Wshadow -Wstack-protector -Wstrict-overflow=3
CFLAGS+=`sdl2-config --cflags --libs`

pixel-ed: pixel-ed.c

clean:
	@-rm pixel-ed 2>/dev/null

depend:
	pkg-config --exists sdl2
	perl -e 'use 5.24.0'
	cpanm --installdeps .

realclean: clean
	@-rm *.core ktrace.out *.bmp 2>/dev/null

.PHONY: clean depend realclean
