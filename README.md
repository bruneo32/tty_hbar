# tty_hbar
Simple configurable horizontal bar for the terminal using ANSI escape codes.

Useful for showing information while using a non-graphical terminal like **ssh**.

# Build
```bash
make
```

# Run
Start the program as a background process.
> Don't worry, the process will exit when you close the terminal.
```bash
./tty_hbar &
```

To stop the program, just kill the process.
```bash
$ jobs
[1]+  Running                 ./tty_hbar &

$ kill %1
[1]+  Stopped                 ./tty_hbar &
```

# Usage
Create a config file (`~/.tty_hbar`) with the commands you want to display.
- Each line is a column
- Hot-reload config file
- You can align the output to the left, center or right
- You can use ANSI escape codes in the output
- You can use the `$PWD`, `$(date)`, etc, like **PS1** shell variable does.

## Example config files

### Example 1
| Column 1 (align left)   | Column 2 (align center) | Column 3 (align right) |
|:------------------------|:-----------------------:|-----------------------:|
| /home/johndoe           |         20:30:40        |    johndoe @ localhost |

```bash
$PWD
$(date +%H:%M:%S)
$USER @ $HOSTNAME
```

### Example 3 (color)
| Column 1 (align left) | Column 2 (align right) |
|:----------------------|-----------------------:|
| /home/johndoe         |             [20:30:40] |

Use [ANSI escape codes](<https://ansi.tools/?s=%255Ce%255B30%253B42m%2520%252Fhome%252Fjohndoe%2520%2520%2520%2520%2520%2520%2520%2520%255Ce%255B1%253B31m20%253A30%253A40>) to bring life to the output.
- Use `\e[30;42m`  to color the output as **black** foreground, **green** background.
- Use `\e[1;31m` to display the time in **bold red**.

```bash
\e[30;42m$PWD
\e[31m[$(date +%H:%M:%S)]
```
