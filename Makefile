CC := gcc
DEBUGFLAGS := `sdl2-config --libs --cflags` `curl-config --libs --cflags` `xml2-config --libs --cflags` -g -Og -ggdb3 -l:libSDL2_ttf.so -l:libSDL2_image.so -fsanitize=address,undefined
OPTFLAGS := `sdl2-config --libs --cflags` `curl-config --libs --cflags` `xml2-config --libs --cflags` -Ofast -l:libSDL2_ttf.so -l:libSDL2_image.so

build: main.c
	$(CC) main.c -o main $(DEBUGFLAGS)

clean:
	rm -f main

optimised: main.c
	$(CC) main.c -o main $(OPTFLAGS)
