# network/http/parser

## Description

The HTTP protocol parser. This serlvet is desgined to read a slow socket connection (Which means the servlet
may push state and pop state multiple times for even a single request). The servlet supports virtual host and
HTTP routing.

## Ports

| Port Name | Type Trait    | Direction | Decription |
|:---------:|:-------------:|:---------:|:-----------|
| `input`   | `plumber/base/Raw` | Input     | The raw HTTP Reuqest Byte Stream |
| `protocol_data` | `plumber/std_servlet/network/http/parser/v0/ProtocolData` | The HTTP protocol releated data, which means it doesn't releated to actual server logic but the response rendering |
| `default` | `plumber/std_servlet/network/http/parser/v0/RequestData` | The default routing output |
| [route-name] | `plumber/std_servlet/network/http/parser/v0/RequestData` | The request data will be forwarded to the [route-name] if the routing rule matches |

## Options

```
network/http/parser [-r|--route <route-rule>] [-r|--route <route-rule] ... [-D|--upgrade-default]
```

The servlet initiazation arguments used to initialize the HTTP routing rules.
By default the servlet will have a default routing rules, which accepts all the request that cannot be matched by any other routing rules.
The `--upgrade-default` option controls if we should to generate HTTP redirection response. (For details see the protocol upgrade part of the
note section)

To define other routing rule, use `-r <routing-rule>`. The routing rule has the following form 

* `name:<pipe-name>;prefix:<url-prefix>` for the request that can use plain HTTP
* `name:<pipe-name>;prefix:<url-prefix>;upgrade_http` for the request that should use HTTPS protocol

## Note

### Limit

This servlet currently only supports HTTP 1.0 and 1.1, and HTTP 2.0 support is in the todo list.
And we are planning to implement the multiplexing part of HTTP 2.0 as an IO module, and the other part of 
the protocol should be implemented by this servlet

## Protocol Upgrade

The protocol upgrade mechanism relies on the `protocol_data` field. When the HTTP request parser realized that 
the response generator should issue a HTTP redirect for protocol upgrade purpose, the `protocol_data` will have
the `upgrade_target` field set and all the request data path will leave empty.

The `protocol_data` port should be directly connected to the response generator servlet, because all the `protocol_data`
is the data that only related to the HTTP protocol itself, but not any other additional logic.

In our case, the `upgrade_target` will be recieved by the response generator servlet, and this will finally trigger the
HTTP server issue a HTTP redirection.

## Protocol Defined

* `plumber/std_servlet/network/http/parser/v0/RequestData`
* `plumber/std_servlet/network/http/parser/v0/ProtocolData`

## See Also

* network/http/render
