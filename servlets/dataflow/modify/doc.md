# dataflow/modify

## Description

Modify the typed input `base`.

For example, suppose the `base` has a type of `Triangle`.

```
type Point {
	double x;
	double y;
};

type Triangle {
	Point vertex[3];
};
```

And we have a servlet initialized with the following parameter.

```
dataflow/modify vertex[0].x vertex[1]
```

The pipe port `vertex[0].x` should accept a value of `double` and the pipe port `vertex[1]` should accept a `Point`.

The `output` port should contains another `Triagnle` type which has `vertex[0].x` and `vertex[1]` modified.

## Ports

| Port Name | Type Trait   | Direction | Decription |
|:---------:|:------------:|:---------:|:-----------|
| `base`    | `$BASE`      | Input     | The data we want to modify |
| [field]   | `$M_n`       | Input     | The value the servlet should modifiy the field to |

## Options

```
dataflow/modify field1 ... fieldN
```

