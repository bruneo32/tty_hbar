# tty_hbar
Simple configurable horizontal bar for the terminal using ANSI escape codes.

Useful for showing information while using a non-graphical terminal like **ssh**.

# Build
Install dependencies
```bash
sudo apt install libyaml-dev
```

Build the binary
```bash
sudo make install # Compile and install system-wide
```

# Run
Start the program as a background process.
> Don't worry, the process will exit when you close the terminal.
```bash
tty_hbar &
```

## Run nicely
> Be nice!
You can tell the program to be nicer and use less CPU time with the `nice` command.
```bash
nice -n5 tty_hbar &
```

To stop the program, just kill the process.
```bash
$ jobs
[1]+  Running                   tty_hbar &

$ kill %1
[1]+  Stopped                   tty_hbar &
```

# Usage
Create a config file (`~/.tty_hbar`) with the commands you want to display.
- Hot-reload config file

## Column properties
- **align**: *left*|*center*|*right*
  - Align the text in the column
- **shrink**: *true*|*false*
  - If **true**, the column will be shrinked to fit the text, making the neighbor columns grow
- **color**:
  - [ANSI color code](<https://www.dev-toolbox.tech/tools/ansi-color-reference>) to color the whole column.
  - **Example**: `color: 97;44` makes **bright white** text on a **blue** background column.
- **cmd**:
  - The text to display in the column
  - You can use **ANSI escape codes** in the output
  - You can evaluate bash expressions like `$PWD`, `$(date)`, etc.

## Example config files

### Example 1
| Column 1 (align left)   | Column 2 (align left) |
|:------------------------|:----------------------|
| hello                   |                 world |

```yaml
- cmd: hello
- cmd: world
```

### Example 2 (align)
| Column 1 (align left) | Column 2 (align center) | Column 3 (align right) |
|:----------------------|:-----------------------:|-----------------------:|
| /home/johndoe         |        [20:30:40]       |      johndoe@localhost |

```yaml
-
  align: left
  cmd: $PWD
-
  align: center
  cmd: "[$(date +%H:%M:%S)]"
-
  align: right
  cmd: "$USER@$HOSTNAME"
```

### Example 3 (color)
| Column 1 (align left) | Column 2 (align right) |
|:----------------------|-----------------------:|
| /home/johndoe         |              localhost |

Use [ANSI escape codes](<https://ansi.tools/?s=%255Ce%255B30%253B42m%2520%252Fhome%252Fjohndoe%2520%2520%2520%2520%2520%2520%2520%2520%255Ce%255B1%253B31mlocalhost>) to bring life to the output.
- Use `\e[30;42m`  to color the output as **black** foreground, **green** background.
- Use `\e[1;31m` to display the hostname in **bold red**.

```yaml
-
  align: left
  color: "30;42"
  cmd: $PWD
-
  align: right
  color: "1;31"
  cmd: "$HOSTNAME"
```

### Example 4 (shrink)
| Column 1 (align left)                 | Column 2 (align right) |
|:--------------------------------------|-----------------------:|
| /home/johndoe                         |             [20:30:40] |

In this case, PWD can be very long but the date will always be small.
So we can shrink the date column to make space for the PWD column.

```yaml
-
  align: left
  color: "30;42"
  cmd: $PWD
-
  align: right
  color: "1;31"
  shrink: true
  cmd: "[$(date +%H:%M:%S)]"
```
