nim: main.c
	$(CC) main.c -o nim.o # -Wall -Wextra -pedantic -std=c99

clean:
	rm nim.o
