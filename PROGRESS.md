# Shelly - Development Progress

## Project
A Unix-like command-line shell written in C.

## Environment
- Windows 11 + WSL2
- Ubuntu 24.04
- GCC 13.3
- Development language: C

## Completed
- Basic REPL using getline()
- Dynamic memory management
- Tokenization using strtok()
- Process creation using fork()
- Command execution using execvp()
- Parent process waiting using waitpid()
- cd built-in using chdir()

## Concepts learned
- Pointers and pointer-to-pointer
- malloc, realloc, free
- String manipulation
- Processes and system calls

## Current state
External commands work.
cd works, including changing directories.
exit is not implemented yet.

## Next tasks
1. Implement exit
2. Make cd without arguments go to HOME
3. Implement help
4. Improve command parsing and support quotes
5. Implement redirection and pipes

## Learning approach
Follow Brennan's Write a Shell in C tutorial.
Implement incrementally and understand every component.

## Completed: Quote-aware tokenization

- Replaced `strtok()` with a custom tokenizer.
- Added basic double-quote support.
- Spaces inside double quotes are preserved.
- Supports normal arguments and multiple quoted/unquoted arguments.
- Tested commands:
  - `echo hello world`
  - `echo "hello world"`
  - `echo "hello" world`
  - `echo "hello        world"`

### Known limitations
- Single quotes not supported.
- Escape characters not supported.
- Unclosed quotes are not handled.
- Tabs are not treated as separators.

## Next milestone
- Input/output redirection.
- Learn file descriptors, `open()`, `dup2()`, and `close()`.

## Completed: I/O Redirection

- Implemented output redirection (`>`).
- Implemented append redirection (`>>`).
- Implemented input redirection (`<`).
- Used `open()` to open and create files.
- Used `dup2()` to redirect standard input/output.
- Used `close()` to release file descriptors.
- Implemented redirection in the child process.

### Tested commands

- `echo hello > output.txt`
- `echo world >> output.txt`
- `cat output.txt`
- `wc -w < output.txt`

### Known limitations

- Redirection operators require spaces around them.
- Full shell parsing and advanced redirection are not supported yet.

## Next milestone

- Pipes (`|`).
- Signal handling (`Ctrl+C`, `Ctrl+Z`).
- Command history.

## Pipes
- [x] Implemented single pipe (`|`) between two commands
- [x] Connected commands using `pipe()` and `dup2()`
- [x] Supported pipes with input/output redirection
- [ ] Support multiple pipes
- [ ] Improve error handling

## Command History
- [x] Character-by-character terminal input using termios
- [x] Up/Down arrow history navigation
- [x] Backspace support
- [x] In-memory history (100 commands)
- [ ] Persistent history across sessions

## Pipes
- [x] Implemented multiple pipes between commands
- [x] Connected processes using pipe() and dup2()
- [x] Supported pipes with input/output redirection
- [x] Added invalid pipe syntax detection
