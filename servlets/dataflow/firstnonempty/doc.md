# dataflow/firstnonempty

## Description

Forward the first non-empty input to the output. If all the inputs are empty, just leave the output port untouched.

This is the default merger of PScript dataflow literal. When there are multiple outputs connected to the same downstream port,
The servlet will be added automatically, so that, the first non-empty port will be forwarded to the output.

## Ports

| Port Name | Type Trait                          | Direction | Decription |
|:---------:|:-----------------------------------:|:---------:|:----------:|
|  `inN`    | `$T`  | Input     | The n-th Input |
|  `out`    | `$T`` | Output    | The output |

## Options

```
dataflow/firstnonempty N
```
where `N` is the number of inputs.

