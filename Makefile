FLAGS := `sdl2-config --libs --cflags` `curl-config --libs --cflags` `xml2-config --libs --cflags` -l:libSDL2_ttf.so -l:libSDL2_image.so
DEBUGFLAGS := $(FLAGS) -g -Og -ggdb3
OPTFLAGS := $(FLAGS) -Ofast -flto -s

optimised: ersatz.c
	$(CC) ersatz.c -o ersatz $(OPTFLAGS)

debug: ersatz.c
	$(CC) ersatz.c -o ersatz $(DEBUGFLAGS)

clean:
	rm -f ersatz
