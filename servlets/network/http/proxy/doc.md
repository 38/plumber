# network/http/proxy

## Description

The HTTP proxy servlet. This servlet requests a remote server and returns the server response.

In fact during the servlet execution stage, no network IO actually happens, the continaution of 
requesting the remote server will be passed to downstream servlet. For More detailed discuss ,
please read the note section.

As a part of standard servlet collection, the servlet also takes the request data protocol defined
by `network/http/parser`.

Normally to implement the full reverse proxy functionality, we also need a servlet for *request rewriting*.

## Ports

| Port Name | Type Trait  | Direction | Decription |
|:---------:|:-----------:|:---------:|:-----------|
| `request` | `plumber/std_servlet/network/http/parser/v0/RequestData` | Input | The requeset data |
| `response` | `plumber/std_servlet/network/http/proxy/v0/Response` | Output | The response data |

## Options

```
network/http/proxy [-P|--peer-pool-size <size>] [-p|--pool-size <size>] [-T|--timeout <timeout-in-sec>]
  -P  --peer-pool-size    The maximum number of connection that can be perserved per peer
  -p  --pool-size         The connection pool size
  -T  --timeout           The amount of time the socket can wait for data
```

The reverse proxy servlet maintains a connection pool to remote servers.
There are two limits `peer-pool-size` and `pool-size`. 

The `pool-size` limits the total number of connections the servlet can hold.
The `peer-pool-size` limits the number of connections to the same remote server.
The `timeout` arguments changes the time limit for remote server to response.

## Note 

Although both `network/http/client` and this servlet are able to request other HTTP server,
however this servlet is quite different from the `network/http/client` servlet. 

The standard servlet collection actually distinguish how the response is used. The HTTP proxy
servlet, unlike the HTTP client servlet, doesn't actually care about the what the remote HTTP
server response. What the proxy servlet do is only fowarding whatever the real server responded.
However, the client servlet actually take the result and make further processing.

This is a fundamental difference, since for the proxy servlet, we actually can delegate the responsiblity
of fowarding actual data to the IO module's asynchronous IO loop.
However for the HTTP client, the logic in the dataflow graph is depends on the server response. Thus the
servlet **must** wait until the servlet get the result.

This makes two servlet completely different. For the proxy servlet, it actually produce a RLS object of
the continuation for response forwarding, and no network IO happened at the time the servlet gets executed.
This allows the servlet implemented as the normal servlet.

However, the client servlet, it uses libcurl requests the remote HTTP server and wait the server responded completely.
Thus it's implemented as an asynchronous servlet, which allows the single-threaded concurrency.

### Limit 

Currently we only support HTTP protocol. But we are planning to make this servlet support more protocol in the future.

### Defined Protocols

* `plumber/std_servlet/network/http/proxy/v0/Response`

### See Also

* `network/http/client`

