# dataflow/demux

## Description 

The dataflow demultiplexier. The servlet is used to implement the dataflow branch.

The servlet will match the condition string to determine with of the output should be activated. 
If there's no pattern has been matched, activated the default port.

Once an output is activated, the data from the data input port will be forwarded to that output port.

## Ports 

For Non numeric mode:

| Port Name | Type Trait  | Direction | Decription |
|:---------:|:-----------:|:---------:|:-----------|
|  `cond`   | `plumber/std/request_local/String`  | Input     | The condition string.|
|  `data`   | `$TData`  | Input     | The data input port |
|  `outN`   | `$TData`  | Output    | The N-th output port, activated when N-th pattern matches the condition string |
|  `default`| `$TData`  | Output    | The port to forward the data by default |

For Numeric Model:

| Port Name | Type Trait  | Direction | Decription |
|:---------:|:-----------:|:---------:|:-----------|
|  `cond`   | `$TCond`  | Input     | The condition string.|
|  `data`   | `$TData`  | Input     | The data input port |
|  `outN`   | `$TData`  | Output    | The N-th output port, activated when N-th pattern matches the condition string |
|  `default`| `$TData`  | Output    | The port to forward the data by default |

## Options

**String Mode**

```
dataflow/demux [--regex] pattern1 ... patternN
```

* `--regex` The regular expression mode - Instead of doing simple string match, use regular expression to match the string reads from `cond` instead

**Numeric Mode**

``` 
dataflow/demux --numeric N 
```

* `--numeric field N` The numeric mode - Instead of doing string match, use numeric selection. The field `$TCond.field` will be read, and use to decide which port should be activated

# Note 

Instead copying the actual data, the output is forked from input. Thus there's no actual data copy happened.
