CC=gcc
CFLAGS=-O2
LDFLAGS=-lfftw3 -lSDL2 -lm

all: audio_vis

audio_vis: src/audio_vis.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f audio_vis
