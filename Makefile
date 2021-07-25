CC=gcc

build: main.c
	$(CC) main.c -o main

clean:
	rm -f main
