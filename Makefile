CC=gcc

build: main.c
	$(CC) main.c -lcurl -lxml2 -o main -I/usr/local/include/libxml2 -g -Og -ggdb3

clean:
	rm -f main
