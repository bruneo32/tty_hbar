#define DELAY_MS         200
#define CMD_BUFSIZE      4096
#define TTYHBAR_FILENAME ".tty_hbar"

#define _XOPEN_SOURCE 600 // Required for wcwidth()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>

// ANSI escape codes
// https://en.wikipedia.org/wiki/ANSI_escape_code
// https://gist.github.com/ConnerWill/d4b6c776b509add763e17f9f113fd25b
#define ESC "\033"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

void sigint_handler(int signum) {
	printf(ESC "8");  // Restore cursor [DEC]
	putchar('\n');
	exit(0);
}

size_t file_count_lines(const char *filepath) {
	FILE *file = fopen(filepath, "r");
	if (!file)
		return 0;

	// Read the file in 64KB chunks
	char buffer[64 * 1024];
	size_t lines = 0;
	size_t bytes_read;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		const char *end = buffer + bytes_read;
		char *p = buffer;

		while ((p = memchr(p, '\n', end - p))) {
			lines++;
			p++;
		}
	}

	fclose(file);
	return lines;
}

size_t count_display_width(const char *str) {
	size_t width = 0;

	mbstate_t state = {0};

	while (*str != '\0') {
		// Catch and skip ANSI sequences FIRST
		if (*str == '\033') {
			if (str[1] == '[') {
				str += 2;
				// CSI sequences end with a character in 0x40 (@) to 0x7E (~)
				while (*str != '\0' && (*str < '@' || *str > '~'))
					str++;
				if (*str != '\0')
					str++;
			} else {
				// Skip lonely ESC char
				str++;
			}
			continue;
		}

		// Decode the UTF-8 bytes into a single wide character
		wchar_t wc;
		size_t bytes = mbrtowc(&wc, str, MB_CUR_MAX, &state);

		if (bytes == (size_t)-1 || bytes == (size_t)-2) {
			// Invalid or incomplete UTF-8 byte
			// Fallback: count as 1 column
			width += 1;
			str += 1;
			memset(&state, 0, sizeof(state));
		} else if (bytes == 0) {
			// Reached the null terminator
			break;
		} else {
			// Ask the system how many TTY columns this character takes
			int char_width = wcwidth(wc);

			// wcwidth returns -1 for unprintable/control characters
			// We only add to the width if it occupies 1 or 2 columns
			if (char_width > 0)
				width += char_width;
			str += bytes;
		}
	}

	return width;
}

int main(int argc, const char *argv[]) {
	struct winsize w;
	char config_path[256];

	// Register signal handlers
	signal(SIGHUP, sigint_handler);
	signal(SIGINT, sigint_handler);
	signal(SIGQUIT, sigint_handler);
	signal(SIGPIPE, sigint_handler);
	signal(SIGTERM, sigint_handler);

	// Enable UTF-8 parsing
	setlocale(LC_ALL, "");

	// Resolve the config file path
	const char *home = getenv("HOME");
	if (home)
		snprintf(config_path, sizeof(config_path), "%s/" TTYHBAR_FILENAME, home);
	else
		// Fallback to the current directory
		snprintf(config_path, sizeof(config_path), TTYHBAR_FILENAME);

	// If the config file doesn't exist, exit
	struct stat st;
	if (stat(config_path, &st) != 0) {
		eprintf("Config file not found. Create a file at ~/" TTYHBAR_FILENAME "\n");
		return 1;
	}

	// Allocate one page for the commands
	char *cmd = malloc(CMD_BUFSIZE);
	if (!cmd)
		return 1;

	// Get parent process environment file
	const pid_t parent_pid = getppid();

loop:
	// Get the terminal size
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	printf(
		ESC "7"    // Save cursor [DEC]: position, attributes, etc
		ESC "[H"   // Go to 0:0
		ESC "[2K"  // Clear line
	);

	const size_t cmd_lines = file_count_lines(config_path);
	const size_t segcol_width = w.ws_col / cmd_lines;
	const size_t segcol_rem   = w.ws_col % cmd_lines;

	for (size_t ln = 0; ln < cmd_lines; ln++) {
		memset(cmd, 0, CMD_BUFSIZE);
		snprintf(cmd, CMD_BUFSIZE - 1,
			// Export all environment variables from the parent process to the subshell
			"bash -c 'while IFS= read -r -d \"\" v; do export \"$v\"; done < /proc/%d/environ; "
			// Execute the command with the CWD of the parent process
			"PWD=$(readlink /proc/%d/cwd) eval \"echo -e \\\"$(sed -n '%zup' '%s')\\\"\"'",
			parent_pid, parent_pid, ln + 1, config_path);

		const size_t segment_width = segcol_width + (ln < segcol_rem ? 1 : 0);

		FILE *fp = popen(cmd, "r");
		if (fp) {
			char buf[segment_width]; // Maximum segment width
			memset(buf, 0, segment_width);
			char *line = fgets(buf, segment_width, fp);

			if (line) {
				// Remove newlines (if any)
				line[strcspn(line, "\n")] = 0;

				// If the line is too long, add a "+" at the end
				if (line[segment_width - 2] != 0)
					line[segment_width - 2] = '+';

				const size_t len = count_display_width(line);
				const int pad = segment_width - len;

				// Print the output into the hbar segment
				if (ln == cmd_lines - 1) {
					// Last line, align right
					printf("%*s%s", pad, "", line);
				} else if (ln == 0) {
					// First line, align left
					printf("%s%*s", line, pad, "");
				} else {
					// Other lines, align center
					const int pad_left = pad / 2;
					const int pad_right = pad - pad_left;
					printf("%*s%s%*s", pad_left, "", line, pad_right, "");
				}
			}

			// Close the pipe
			pclose(fp);
		}
	}

	printf(ESC "8");	 // Restore cursor [DEC]
	fflush(stdout);      // Flush the output (send to the terminal)
	usleep(DELAY_MS * 1000);  // sleep 200ms
	goto loop;

	sigint_handler(0); // Exit
}
