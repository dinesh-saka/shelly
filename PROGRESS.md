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
