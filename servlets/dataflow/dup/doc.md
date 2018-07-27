# dataflow/dup

# Description

The input duplicator. It duplicates the input to `N` outputs. 
This servlet is used to duplicate one data to several different copies, so the same output can be used by several different sub-graph.

This servlet is used as the default splitter in the PScript graph literal. Once a pipe port have more than one downstream servlet, a 
node of `dataflow/dup` will be automatically added into graph to split the dataflow.

# Ports

| Port Name | Type Trait                          | Direction | Decription |
|:---------:|:-----------------------------------:|:---------:|:-----------|
|  `in`     | `$T`  | Input     | The input to duplicate |
|  `outN`   | `$T`` | Output    | The output ports which should contains the identical output |

# Options

`dataflow/dup N`

where N is the number of the output should produced

# Note

The output is actually forked from input, thus there's no data copy at all.
