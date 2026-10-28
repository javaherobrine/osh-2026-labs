# Lab 2

## Known Bugs

- `a "|" b` will be processed as `a | b` instead of regarding `|` as a string
- `a > b | c` may be processed incorrectly 

## Tracing

An `eBPF` program is provided to trace syscalls. I'll provide this program with `bpftrace` tool instead of `libbpf` program which is so complex that I don't need to use it here. I'll output the `pid` of the shell first, then use this command to trace its operations:

> sudo bpftrace trace.bpftrace | grep $pid

Parameter `$pid` is what showed in the beginning of the shell.

## Optional Functions Supported

- `Ctrl+D` to exit the terminal
- Here document
- `cd -` and `cd` with 0 parameters
- builtin commands in pipeline
- `fg` and `bg`
- `su` will switch user to `root` if no parameters
- `sudo` in builtin commands and pipelines
- builtin `kill` (Different UX: `kill pid signal` instead of `kill -signal pid`)
