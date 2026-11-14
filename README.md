# What the Shelly?


Shelly is a lightweight Unix-like command-line shell written in C. It
was built as a systems programming project to explore process
management, Unix system calls, command parsing, file descriptors, pipes,
signals, and terminal input handling.

## Features

-   **Command execution:** Run external programs using `fork()` and
    `execvp()`.
-   **Built-in commands:** `cd`, `help`, `exit`, `jobs`, and `wait`.
-   **Command parsing:** Supports single quotes, double quotes, escaped
    characters, and environment variable expansion.
-   **I/O redirection:** Supports input (`<`), output (`>`), and append
    (`>>`) redirection.
-   **Pipelines:** Connect commands using `|`.
-   **Command chaining:** Supports `;`, `&&`, and `||`.
-   **Background execution:** Run commands in the background using `&`.
-   **Job management:** Inspect background jobs using `jobs` and wait
    for them using `wait`.
-   **Command history:** Navigate previous commands using the Up and
    Down arrow keys. History is persisted in `~/.shelly_history`.
-   **Interactive line editing:** Supports Left/Right, Home/End, Ctrl+A,
    Ctrl+E, Backspace, and Delete.
-   **Signal handling:** Handles Ctrl+C without terminating the shell
    and exits on Ctrl+D.
-   **Dynamic prompt:** Displays the current working directory.

## Requirements

-   Linux or WSL2
-   GCC
-   A terminal that supports ANSI escape sequences

## Build and Run

Clone the repository:

``` bash
git clone https://github.com/YOUR_USERNAME/shelly.git
cd shelly
```

Compile:

``` bash
gcc -Wall -Wextra -Wpedantic -o shelly main.c
```

Run:

``` bash
./shelly
```

## Examples

Run a command:

``` bash
ls -la
```

Use pipes:

``` bash
cat main.c | grep include | wc -l
```

Redirect output:

``` bash
echo "Hello, Shelly" > output.txt
```

Append output:

``` bash
echo "Another line" >> output.txt
```

Chain commands:

``` bash
echo first ; echo second
```

Conditional execution:

``` bash
false && echo "This will not run"
false || echo "This will run"
```

Run a background process:

``` bash
sleep 10 &
jobs
wait
```

Expand environment variables:

``` bash
echo $HOME
echo "${USER}"
```

## Built-in Commands

  Command   Description
  --------- --------------------------------------
  `cd`      Change the current working directory
  `help`    Display available built-in commands
  `exit`    Exit Shelly
  `jobs`    List background jobs
  `wait`    Wait for background jobs to finish

## Project Structure

``` text
shelly/
├── main.c
├── README.md
└── PROGRESS.md
```

## Scope and Limitations

Shelly is an educational shell implementation, not a full replacement
for Bash or Zsh. It does not aim to support every feature of a
production shell, such as shell scripting, advanced job control, or the
complete POSIX shell language.

## License

This project is open source. See the repository's license file for usage
terms.
