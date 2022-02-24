CC := gcc
FLAGS := `sdl2-config --libs --cflags` `curl-config --libs --cflags` `xml2-config --libs --cflags` -l:libSDL2_ttf.so -l:libSDL2_image.so
DEBUGFLAGS := $(FLAGS) -g -Og -ggdb3 #-fsanitize=address,undefined
OPTFLAGS := $(FLAGS) -Ofast -flto -s

build: main.c
	$(CC) main.c -o main $(DEBUGFLAGS)

clean:
	rm -f main

optimised: main.c
	$(CC) main.c -o main $(OPTFLAGS)
