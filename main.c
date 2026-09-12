#define DELAY_MS         200
#define CMD_BUFSIZE      4096
#define TTYHBAR_FILENAME ".tty_hbar.yml"
#define MAX_COLUMNS  16

#define _XOPEN_SOURCE 600 // Required for wcwidth()
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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

// https://wpsoftware.net/andrew/pages/libyaml.html
#include <yaml.h>

// ANSI escape codes
// https://en.wikipedia.org/wiki/ANSI_escape_code
// https://gist.github.com/ConnerWill/d4b6c776b509add763e17f9f113fd25b
#define ESC "\033"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define streq(s1, s2) (strcmp((s1), (s2)) == 0)

#define max(a, b) ({ \
	__typeof__ (a) _a = (a); \
	__typeof__ (b) _b = (b); \
	_a > _b ? _a : _b; \
})
#define min(a, b) ({ \
	__typeof__ (a) _a = (a); \
	__typeof__ (b) _b = (b); \
	_a < _b ? _a : _b; \
})


typedef unsigned char Align;
enum Align {
	AlignLeft,
	AlignCenter,
	AlignRight
};

typedef struct ColumnConfig ColumnConfig;
struct ColumnConfig {
	bool shrink;
	Align align;
	Align flow;
	char *color;
	char *cmd;
};

ColumnConfig default_col_data = {
	.shrink = false,
	.align = AlignLeft,
	.flow = AlignRight,
	.color = NULL,
	.cmd = NULL,
};

// Config file usually won't change or just change very little
// So the data on each parse will be almost the same, hence we can reuse
// the existing ColumnConfig structures instead of allocating new ones each time
ColumnConfig columns[MAX_COLUMNS] = {0};
size_t num_columns = 0;

void parse_config(const char *filepath) {
	FILE *yfile = fopen(filepath, "r");
	yaml_parser_t yparser;
	yaml_event_t  yevent;

	/* Initialize parser */
	if (!yaml_parser_initialize(&yparser))
		eprintf("Failed to initialize parser!\n");
	if (!yfile)
		eprintf("Failed to open file!\n");

	yaml_parser_set_input_file(&yparser, yfile);

	size_t depth = 0;

	enum e_states {
		STATE_NONE = 0,
		STATE_GET_KEY,
		STATE_GET_VALUE
	};
	char state = STATE_NONE;

	enum e_keys {
		KEY_NONE = 0,
		KEY_SHRINK,
		KEY_ALIGN,
		KEY_FLOW,
		KEY_COLOR,
		KEY_CMD
	};
	char key = KEY_NONE;

	// Reset columns array WITHOUT ERASING THE DATA
	num_columns = 0;
	ColumnConfig *col = NULL;

	do {
		if (!yaml_parser_parse(&yparser, &yevent)) {
			eprintf("Parser error %d\n", yparser.error);
			exit(EXIT_FAILURE);
		}

		switch (yevent.type) {
			case YAML_SEQUENCE_START_EVENT:
				depth++;
				break;
			case YAML_SEQUENCE_END_EVENT:
				depth--;
				break;

			case YAML_MAPPING_START_EVENT:
				if (depth != 1)
					break;

				// Prepare new column
				col = &columns[num_columns];
				// Preset defaults
				col->shrink = default_col_data.shrink;
				col->align = default_col_data.align;
				col->flow = default_col_data.flow;
				// Do not preset strings
				state = STATE_GET_KEY;
				break;

			case YAML_MAPPING_END_EVENT:
				if (depth != 1)
					break;
				// Finalize the current column configuration
				if (num_columns < MAX_COLUMNS) {
					num_columns++;
				} else {
					eprintf("Maximum number of columns reached!\n");
					goto cleanup;
				}
				break;

			case YAML_SCALAR_EVENT: {
				switch (state) {
					case STATE_GET_KEY: {
						char *str = (char *)yevent.data.scalar.value;

						if (streq(str, "shrink")) {
							key = KEY_SHRINK;
						} else if (streq(str, "align")) {
							key = KEY_ALIGN;
						} else if (streq(str, "flow")) {
							key = KEY_FLOW;
						} else if (streq(str, "color")) {
							key = KEY_COLOR;
						} else if (streq(str, "cmd")) {
							key = KEY_CMD;
						} else {
							key = KEY_NONE;
						}

						// Next is a value for this key
						state = STATE_GET_VALUE;
					} break;

					case STATE_GET_VALUE: {
						char *str = (char *)yevent.data.scalar.value;

						switch (key) {
							case KEY_SHRINK:
								if (streq(str, "true"))
									col->shrink = true;
								// Default value is false
								break;
							case KEY_ALIGN:
								if (streq(str, "center"))
									col->align = AlignCenter;
								else if (streq(str, "right"))
									col->align = AlignRight;
								// Default value is 0 (AlignLeft)
								break;
							case KEY_FLOW:
								if (streq(str, "center"))
									col->flow = AlignCenter;
								else if (streq(str, "right"))
									col->flow = AlignRight;
								// Default value is 0 (AlignLeft)
								break;
							case KEY_COLOR:
								if (col->color) {
									// If the new command is the same as the current one, do nothing
									// This is practically free because modern CPUs have vectorization
									// and branch prediction, so the overhead is minimal
									if (streq(str, col->color))
										break;
									// New string, free the previous one
									free(col->color);
								}
								// Copy str to column
								col->color = strdup(str);
								break;
							case KEY_CMD:
								if (col->cmd) {
									// If the new command is the same as the current one, do nothing
									if (streq(str, col->cmd))
										break;
									// New string, free the previous one
									free(col->cmd);
								}
								// Copy str to column
								col->cmd = strdup(str);
								break;
							default:
								eprintf("Unknown key: %s\n", str);
								break;
						}

						// Reset state for the next key-value pair
						state = STATE_GET_KEY;
					} break;
				}
			} break;
		}
	} while (yevent.type != YAML_STREAM_END_EVENT);

cleanup:
	yaml_event_delete(&yevent);
	yaml_parser_delete(&yparser);
	fclose(yfile);
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

void sigint_handler(int signum) {
	printf(ESC "8");  // Restore cursor [DEC]
	putchar('\n');
	exit(0);
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

	// Get parent pid and gid
	const pid_t parent_pid = getppid();
	const pid_t parent_gid = getpgid(parent_pid);

	// Get the parent process CWD symlink
	char proc_parent_cwd[256] = {0};
	snprintf(proc_parent_cwd, sizeof(proc_parent_cwd), "/proc/%d/cwd", parent_pid);

loop:
	// Check if the shell is the current foreground process of the TTY.
	// If it is NOT, it means a command like nano, less, or vim is running.
	// We pause rendering to avoid clobbering their UI.
	if (tcgetpgrp(STDOUT_FILENO) != parent_gid) {
		usleep(DELAY_MS * 1000);
		goto loop;
	}

	// Get the terminal size
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	const typeof(w.ws_col) ws_cols = w.ws_col;

	// Set this process CWD to its parent's
	char parent_cwd[256] = {0};
	readlink(proc_parent_cwd, parent_cwd, sizeof(parent_cwd));
	chdir(parent_cwd);

	// Read config_path and update columns
	parse_config(config_path);

	// Processes cmd output
	char pbufs[num_columns][ws_cols];
	memset(pbufs, 0, sizeof(pbufs));

	// Fetch the output for each command into the pbufs array
	for (size_t col_idx = 0; col_idx < num_columns; col_idx++) {
		const ColumnConfig *col = &columns[col_idx];
		char *buf = pbufs[col_idx];

		snprintf(cmd, CMD_BUFSIZE - 1,
			// Export all environment variables from the parent process to the subshell
			"bash -c 'while IFS= read -r -d \"\" v; do export \"$v\"; done < /proc/%d/environ; "
			// Execute the command with the CWD of the parent process
			"export PWD=$(pwd); eval \"echo -e \\\"%s\\\"\"'",
			parent_pid, col->cmd);

		FILE *fp = popen(cmd, "r");
		if (fp) {
			fgets(buf, ws_cols, fp);
			if (buf[0])
				// Remove newlines (if any)
				buf[strcspn(buf, "\n")] = 0;
			pclose(fp);
		}
	}

	// Calculate the size of each column
	size_t segments_width[num_columns];
	const size_t segcol_width = w.ws_col / num_columns;
	const size_t segcol_rem   = w.ws_col % num_columns;

	// Initial values
	for (size_t col_idx = 0; col_idx < num_columns; col_idx++)
		segments_width[col_idx] = segcol_width + (col_idx < segcol_rem ? 1 : 0);

	// Only calculate shrink and grow if there is more than one column
	if (num_columns > 1)
		for (ssize_t col_idx = 0; col_idx < num_columns; col_idx++) {
			const ColumnConfig *col = &columns[col_idx];
			const size_t colspan = segments_width[col_idx];

			if (col->shrink) {
				// Shrink this column
				segments_width[col_idx] = min(segments_width[col_idx], count_display_width(pbufs[col_idx]));

				// Grow adjacent columns
				const size_t shrinked = colspan - segments_width[col_idx];
				if (!shrinked)
					continue;

				if (col_idx == 0) {
					// Grow the right column ALWAYS
					// If the right column is shrinked it will pass all the accumulated width
					// to the right until some non-shrink column consumes it
					segments_width[col_idx + 1] += shrinked;
				} else if (col_idx == num_columns - 1) {
					// Pass back the remaining accumulated width to the first
					// non-shrinked column on the left
					ssize_t lcol_idx = col_idx - 1;
					while (lcol_idx >= 0 && columns[lcol_idx].shrink)
						lcol_idx--;

					if (lcol_idx >= 0)
						// Grow the found column
						segments_width[lcol_idx] += shrinked;
					else
						// We are all shrinked, unlucky
						segments_width[col_idx] = colspan;
				} else {
					const bool l_shrink = columns[col_idx - 1].shrink;
					const bool r_shrink = columns[col_idx + 1].shrink;

					if (l_shrink) {
						// If the left column is shrinked, grow the right one
						// even if the right column will shrink (right will pass the grow)
						// to the next one
						segments_width[col_idx + 1] += shrinked;
					} else if (r_shrink) {
						// If the right column is shrinked, grow the left one
						segments_width[col_idx - 1] += shrinked;
					} else {
						// Grow both sides
						const size_t grow_w = shrinked / 2;
						segments_width[col_idx - 1] += grow_w;
						segments_width[col_idx + 1] += shrinked - grow_w;
					}
				}
			}
		}

	// Starting printing
	printf(
		ESC "7"    // Save cursor [DEC]: position, attributes, etc
		ESC "[H"   // Go to 0:0
		ESC "[2K"  // Clear line
	);

	for (size_t col_idx = 0; col_idx < num_columns; col_idx++) {
		const ColumnConfig *col = &columns[col_idx];

		if (col->color)
			printf(ESC "[%sm", col->color);

		// Print the prefetched output formatted
		char *line = pbufs[col_idx];
		if (line[0]) {
			const size_t segment_w = segments_width[col_idx] ;
			// TODO: Depends on "flow" attribute
			// If the line is too long, add a "+" at the end (if not shrinked)
			if (segment_w >= segcol_width && line[segment_w- 1] != 0)
				line[segment_w - 1] = '+';

			const size_t len = count_display_width(line);
			const int pad = segment_w - len;

			// Print the output into the hbar segment
			if (col->align == AlignRight) {
				// Align right
				printf("%*s%s", pad, "", line);
			} else if (col->align == AlignCenter) {
				// Align center
				const int pad_left = pad / 2;
				const int pad_right = pad - pad_left;
				printf("%*s%s%*s", pad_left, "", line, pad_right, "");
			} else {
				// Other lines, align left
				printf("%s%*s", line, pad, "");
			}
		}
	}

	printf(ESC "8");	 // Restore cursor [DEC]
	fflush(stdout);      // Flush the output (send to the terminal)
	usleep(DELAY_MS * 1000);  // sleep 200ms
	goto loop;

	// UNREACHABLE
}
