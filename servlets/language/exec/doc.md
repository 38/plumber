# language/exec

## Description

Execute a program and connect `stdin`, `stdout` and `stderr` to a Plumber dataflow graph.

Using this servlet, its able to turn any program, for example a shell script into a Plumebr servlet.

Also, it's possible that we can make Plumber a CGI server  with the help of this servlet.

The child process is launched seperately for each session. Which means unless the IO module can hold the pipe resource,
the process will be killed after one event is handled. 

This servlet also handles the slow pipe as well, which means we are able to use it as the servlet reads and writes from
slow IO modules for example, TCP connections.

## Port

| Port Name | Type Trait  | Direction | Decription |
|:---------:|:-----------:|:---------:|:-----------|
| `stdin`   | `plumber/base/Raw` | Input | The standard input of the target program | 
| `stdout`  | `plumber/base/Raw` | Output | The standard output of the target program |
| `stderr`  | `plumber/base/Raw` | Output | The standard error of the target program |

## Options

```
language/exec <command> <command-arg1> .... <command-argN>
```

## Note

The servlet is actually quite simple, it doesn't assume the line-based input and feed data as much as it can.

However, it doesn't implements a process pool, which could be quite slow when we must handle a lot of new sessions.
