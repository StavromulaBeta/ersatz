CC := gcc
CFLAGS := `sdl2-config --libs --cflags` `curl-config --libs --cflags` `xml2-config --libs --cflags` -g -Og -ggdb3 -l:libSDL2_ttf.so -l:libSDL2_image.so -fsanitize=address

build: main.c
	$(CC) main.c -o main $(CFLAGS)

clean:
	rm -f main
