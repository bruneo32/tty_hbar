.PHONY: clean install

tty_hbar: main.c
	$(CC) -Wall -Wno-switch -O2 -s $^ -o $@ -lyaml

clean:
	rm -f tty_hbar

install: tty_hbar
	mkdir -p /usr/local/bin
	mv -f tty_hbar /usr/local/bin/tty_hbar
