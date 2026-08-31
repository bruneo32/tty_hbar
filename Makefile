.PHONY: clean

tty_hbar: main.c
	$(CC) -O2 -s $^ -o $@

clean:
	rm -f tty_hbar
