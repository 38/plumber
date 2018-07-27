# dataflow/regex

## Description

The regular expression filter. This servlet filters the input based on the regular expression sepecified in the servlet init parameter.

If the input matches the regular expression, foward the input to output. Otherwise, leave the output empty.

## Ports

For the default mode:

| Port Name | Type Trait                          | Direction | Decription |
|:---------:|:-----------------------------------:|:---------:|:----------:|
| `input`   | `plumber/std/request_local/String`  | Input     | The input string which we need to match |
| `output`  | `plumber/std/request_local/String`  | Output    | The output |

For the raw-input mode:

| Port Name | Type Trait                          | Direction | Decription |
|:---------:|:-----------------------------------:|:---------:|:----------:|
| `input`   | `plumber/base/Raw`  | Input     | The input string which we need to match |
| `output`  | `plumber/base/Raw`  | Output    | The output |

## Options

```
dataflow/regex [-D|delim] [-F|--full] [-I|--inverse] [-L|--max-line-size] [-R|--raw-input] [-s|--simple] <pattern>

  -D  --delim            Set the end-of-line marker
  -F  --full             Turn on the full-line-matching mode
  -h  --help             Show this help message
  -I  --inverse          Do inverse match, filter all the matched string out
  -L  --max-line-size    Set the maximum line buffer size in kilobytes (Default: 4096k)
  -R  --raw-input        Read from the untyped input pipe instead of string pipe
  -s  --simple           Simple mode, do simple string match with KMP algorithm
```

Where pattern is the input pattern. 

Use `--raw-input` to make the servlet run as raw mode, which reads an untyped pipe port and interpet it as a string.

## Note

The servlet is build on top of libpcre, and it accepts the PCRE-style regular expression.
