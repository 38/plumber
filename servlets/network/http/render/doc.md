# network/http/render

## Description

The HTTP reseponse render servlet.
This is the standard servlet which renders a HTTP response to the remote client.
As a part of the Plumber standard servlet collection, it uses the Plumber  standard
HTTP request/response representation.

This servlet takes the structured response and generate the HTTP response that  external client can understand.

## Ports

| Port Name | Type Trait  | Direction | Decription |
|:---------:|:-----------:|:---------:|:-----------|
|`response`|`plumber/std_servlet/network/http/render/v0/Response`|Input| The structured response to be rendered |
|`protoco_data`|`plumber/std_servlet/network/http/parser/v0/ProtocolData|Input| The protocol releated data, which is parsed from the HTTP request and have nothing to do with actual logic|
|`500`| `plumber/base/Raw` | Input | The signal pipe indicates that the server has encounter an internal error. This is the error handling mechanism, please read note for details|
|`output`| `plumber/base/Raw`|Output|The actual HTTP response the servlet has rendered |
|`proxy`|`plumber/std_servlet/network/http/proxy/v0/Response`| Input | The reverse proxy result. This port only aviable if the servlet is configured to enable the proxy |

## Options

```
network/http/render [--400-mime <bad-request-error-mime-type>] \
                    [-b|--400-page <bad-request-page>] \
					[-406-mime <Not-Acceptable-page-mime-type>] \
					[-4|--406-page <not-acceptable-page>] \
					[-500-mime <internal-error-mime-type>] \
					[-5|--500-page <internal-error-page>] \
					[--503-mime <service-not-avaiable-mime>] \
					[-e|--503-page <service-not-aviable-page] \
					[-C|--chunk-size <number-of-pages-for-one-chunk>] \
					[-c|--chunked] [-L|--compression-level <compression-level>] [-d|--deflate] [-g|--gzip] \
					[-P|--proxy] [-s|--servet-name <servet-name]

      --400-mime             Type of Bad Request Page
  -b  --400-page             Bad Request Page
      --406-mime             Type of Not-Acceptable Error page
  -4  --406-page             Not-Acceptable Error page
      --500-mime             Type of Server Internal Error page
  -5  --500-page             Server Internal Error page
      --503-mime             Type of Service Not Available Error page
  -e  --503-page             Service Not Available Error page
  -C  --chunk-size           The maximum chunk size in number of pages
  -c  --chunked              Enable Chunked Encoding
  -L  --compression-level    The compression level from 0 to 9
  -d  --deflate              Enable deflate compression
  -g  --gzip                 Enable gzip compression
  -P  --proxy                Enable the reverse proxy support
  -s  --server-name          What we need return for the server name field

```

The servlet have a lot of configurations, which can be categorized into following categories.

### The error page consutomization

The request render is responsible to render the following error pages:
- *400-Bad Request*, this is actually a property of protocol_data, which indicates the request itself is malformed.
- *406-Not Acceptable*, this is a protocl data as well, which means the request parser decide to reject a range request
- *500-Internal Error*, this is caused by the error handling signal
- *503-Service Not Available*, this will caused by the proxy object timed-out.

### Transfer encoding confinguration

Currently we are support the following encoding method:

- Chunked, and we can configure the maximum size of a block within a chunked response
- Deflate, we can use deflate algorithm for compression
- GZip, we can use GZip for response compression

### Reverse Proxy Configuration

To enable the reverse proxy port

### Server Name

We are able to configure the server name with `-s` or `--server-name` option.

It's gerenally reasonable to customize the server name, since it provides additonal information about how to attack the server.

## Note

### Error Handling

When any part of the dataflow graph returns an error, the server could response the client with a 500 error page.
This is done by the error signal pipe `500`. When this port has nonempty data, the servlet will issue an 500 error page anyway.
Basically, the server error handling is as simple as connect all the `__error__` port into the 500 port, thus once any part of the
server went wrong, the 500 page will be returned.

## See Also

- `network/http/parser`
- `network/http/proxy`
