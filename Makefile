.PHONY: clean install

tty_hbar: main.c
	$(CC) -O2 -s $^ -o $@

clean:
	rm -f tty_hbar

install:
	mkdir -p /usr/local/bin
	mv -f tty_hbar /usr/local/bin/tty_hbar
