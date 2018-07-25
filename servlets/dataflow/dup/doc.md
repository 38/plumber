# dataflow/dup

# Description

The input duplicator. It duplicates the input to `N` outputs.

# Ports

| Port Name | Type Trait                          | Direction | Decription |
|:---------:|:-----------------------------------:|:---------:|:----------:|
|  `in`     | `$T`  | Input     | The input to duplicate |
|  `outN`   | `$T`` | Output    | The output ports which should contains the identical output |

# Options

`dataflow/dup N`

where N is the number of the output should produced
