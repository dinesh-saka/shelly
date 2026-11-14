# Shelly --- Development Progress

## Overview

Shelly is a Unix-like command-line shell written in C. The project
focuses on understanding how shells interact with the operating system
through process creation, execution, file descriptors, signals, and
terminal input.

## Implemented Features

### 1. Shell Fundamentals

-   [x] Interactive read-evaluate-print loop (REPL)
-   [x] Dynamic prompt displaying the current working directory
-   [x] External command execution using `fork()` and `execvp()`
-   [x] Process waiting using `waitpid()`
-   [x] Built-in commands: `cd`, `help`, and `exit`

### 2. Command Parsing

-   [x] Tokenization of command input
-   [x] Single-quoted and double-quoted arguments
-   [x] Escaped characters
-   [x] Adjacent quoted and unquoted segments
-   [x] Environment variable expansion
-   [x] Unmatched quote detection

### 3. Redirection and Pipelines

-   [x] Output redirection (`>`)
-   [x] Append redirection (`>>`)
-   [x] Input redirection (`<`)
-   [x] Command pipelines (`|`)
-   [x] Multiple commands in a pipeline

### 4. Command Execution

-   [x] Sequential command execution (`;`)
-   [x] Conditional AND (`&&`)
-   [x] Conditional OR (`||`)
-   [x] Background execution (`&`)

### 5. Process and Job Management

-   [x] Background job tracking
-   [x] Job listing (`jobs`)
-   [x] Waiting for background jobs (`wait`)
-   [x] Background process reaping using `WNOHANG`
-   [x] Background process cleanup when exiting the shell

### 6. Terminal Interaction

-   [x] Ctrl+C signal handling
-   [x] Ctrl+D exit handling
-   [x] Interactive command history
-   [x] Persistent history in `~/.shelly_history`
-   [x] Up/Down history navigation
-   [x] Left/Right cursor movement
-   [x] Backspace and Delete
-   [x] Home and End
-   [x] Ctrl+A and Ctrl+E

### 7. Reliability

-   [x] Error handling for missing commands
-   [x] Error handling for pipe and redirection failures
-   [x] Memory cleanup for command parsing
-   [x] Handling of malformed quoted input

## Testing

The following scenarios have been manually tested:

-   Basic external commands
-   Quoted arguments and escaped characters
-   Environment variable expansion
-   Input and output redirection
-   Append redirection
-   Pipelines
-   Sequential and conditional command execution
-   Background process execution
-   Job listing and waiting
-   Unmatched quotes
-   Shell survival after Ctrl+C
-   Background process cleanup on exit
-   Interactive command history and line editing

## Version 1.0

The initial release focuses on a functional interactive shell with
command execution, parsing, redirection, pipelines, background jobs,
persistent history, and basic terminal line editing.

## Future Ideas

Potential improvements for a future version:

-   Command and path autocomplete
-   Inline command suggestions
-   Improved history search
-   Directory bookmarks
-   Git-aware prompt
-   Interactive process dashboard
-   Split panes and multiple terminal sessions

These features are intentionally outside the initial v1.0 scope.
