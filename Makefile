all: nim

options:
	@echo nim install options:
	@echo "DESTDIR  = $(DESTDIR)"
	@echo "PREFIX   = $(PREFIX)"

nim: main.c
	$(CC) main.c -o nim -Wall -Wextra -pedantic -std=c99

install: nim options
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f nim $(DESTDIR)$(PREFIX)/bin

uninstall: 
	rm -f $(DESTDIR)$(PREFIX)/bin/nim

clean:
	rm nim
