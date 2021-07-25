CC=gcc

build: main.c
	$(CC) main.c -lcurl -o main

clean:
	rm -f main
