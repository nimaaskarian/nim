include config.mk
all: nim

install-options:
	@echo nim install options:
	@echo "DESTDIR  = $(DESTDIR)"
	@echo "PREFIX   = $(PREFIX)"

options:
	@echo nim compile options:
	@echo "CFLAGS    = $(CFLAGS)"

nim: main.c options
	$(CC) main.c -o nim $(CFLAGS)

install: nim install-options
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f nim $(DESTDIR)$(PREFIX)/bin

uninstall: 
	rm -f $(DESTDIR)$(PREFIX)/bin/nim

clean:
	rm nim
