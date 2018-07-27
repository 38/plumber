# dataflow/extract

## Description

The field extractor, which extract the given field from the input.

For example, for the following input

```
type Point {
	double x;
	double y;
};
```

We are able to use the servlet to exact the field `x` from a input with type `Point`.

```
dataflow/extract x
```

## Ports

| Port Name | Type Trait                          | Direction | Decription |
|:---------:|:-----------------------------------:|:---------:|:-----------|
| `input`   | `$T`                                | Input     | The data we want to extract |
| `output`  | `$T.field_name`                     | Output    | The extracted value. `field_name` is determined by the servlet init string |

## Options

```
dataflow/extract <field_name>
```

* `field_name` The name of the field we want to extract. The field name may not be a single identifer, for example `arr[3]` can be used to extract the value of the array `arr`.
   In addition, it's possible to use the servlet to access nested fields for example `arr[3].x` will access the `x` field of the 4-th element in array `arr`

## Note

The field name is validated when the servlet gets initialized. If the field name isn't a member of the input type `$T`, the servlet will failed to perform type inference.
