# filesystem/readfile

## Description

Reads a file on the local disk. This servlet can be used to access the local filesystem, for example the PINS staitc content
servlet uses this servlet as the data source provided to the external client.

The servlet has few input modes, `raw`, `string` and `field=<field-name>`

* `raw` mode takes the raw byte stream as the path
* `string` mode takes the RLS string object
* `field=<field-name>` reads the RLS string object from the field named `<field-name>`
* `http` mode takes the reuqest data parsed by the `network/http/parser` servlet.

The servlet has few output modes, `raw`, `file` and `http`. 

* `raw` mode outputs the file content as the untyped pipe port output
* `file` mode outputs the file content as a file object in RLS
* `http` mode outputs the file content as the data structure the HTTP response render can understand

### HTTP Mode

The servlet provides the HTTP mode, which can be used as the data source component by any static content server.
In addtion to the simple file access, the HTTP mode also produces some HTTP responses.

If the default index file name has been given, e.g. `index.html`, when the `path` is a directory, the servlet will 
look for the index page instead of returning an error. 
If this is the case, HTTP redirection will be issued by the servlet.

For the HTTP output mode, the servlet also produces the error message if the file cannot be accessed.

* If the path is pointed to a directory or the path doesn't exist, the page-not-found error message will be produced.
* If the file cannot be read due to insufficient permission, the page-not-found error message will be produced.
* If the servlet takes an HTTP request as a input and the method isn't either `GET` or `HEAD`, method-disallowed message will be produced
* If the relative path has beyond the root directory, for example `../../password.txt`, forbiden message will be produced
* If a invalid range request has been recieved, range-error messdage will be produced.

The servlet also supports range access under HTTP mode. If the range access is enabled, the servlet can return just a part of the file instead of 
the entire file. 

Under HTTP mode, the servlet will try to guess the MIME type of the file by its extension. The MIME type mapping file can be specified to change the
behavior of the MIME type guesser.

## Ports

| Port Name | Type Trait                          | Direction | Decription |
|:---------:|:-----------------------------------:|:---------:|:----------:|
| `path`    | `plumber/base/Raw` or `plumber/std/request_local/String` or `$T` | Input |The path to the file the servlet should read. ||
| `file`    | `plumber/base/Raw` or `plumber/std/request_local/File` or `plumber/std_servlet/network/http/render/v0/Response` | Output | The file content. |

## Options

```
filesystem/readfile [-C|--compressable <wildcards>] [-d|--default-index] [-D|--default-mime-type <mime-type>] [-F|--forbiden-page <page>] [-i|--index <index>] [-I|--input-mode raw|string|field=...] [-a|--method-not-allowed]
                    [-m|--mime-map-file <mime-map> ] [-M|-moved-page <page>] [-N|--not-found-page <page>] [-O|output-mode raw|file|http] [-R|--range-access] [-S|--range-error <page>] -r <root-dir>
  -C  --compressable          Sepcify the wildcard list of compressable MIME types
  -d  --default-index         Enable the default index page
  -D  --default-mime-type     Sepcify the default MIME type
  -F  --forbiden-page         Sepcify the path to the customized forbiden error page
  -h  --help                  Show this help message
  -i  --index                 Sepcify the list of index file names
  -I  --input-mode            Specify the input mode, possible values: [raw, string, field=<field-expr>]
  -a  --method-not-allowed    Sepcify the path to the customized method not allowed error page
  -m  --mime-map-file         Sepcify the MIME map file
  -M  --moved-page            Sepcify the path to the customized moved page
  -N  --not-found-page        Sepcify the path to the customized not found error page
  -O  --output-mode           Specify the output mode, possible values: [raw, file, http]
  -R  --range-access          Enable the default index page
  -S  --range-error           Sepcify the path to the customized range cannot be satisified error page
  -r  --root                  Sepcify the root directory (Required)
```

The servlet should accept at least the root directory argument `-r` and other modifier can be used as well.

To switch the input mode, use `-I <input-mode>`. To swith the output mode, use `-I <output-mode>`

When the output is `raw`, the file will be completed read to the memory. Otherwise a RLS object pointed to the target file object will be created
and no actual file access happens. All the file access uses the libpstd's file access cache. So the pstd file cache options will change the behavior
of the servlet (For example, the cache entry life time).

When both input and output port are in HTTP mode, the follwoing options will change the behavior of the component.

* For the default index of a directory

	Use `--index index.html` to redirect the directory access to the default index

	Use `--default-index` to enable the default file listing index 

* For the error message customization: 
	
	use `--forbiden-page`, `--method-not-allowed`, `--moved-page`, `--not-found-page` to customize the content of the information/error pages.

* For compressable suggestion

	use `--compressable *.js,*.html` to suggest the HTTP render to compress the file content for all file that maches either `*.js` or `*.html`

* For range access

	use `--range-access` to allow the servlet returns a partial content

# Note 

This servlet implements the core logic of the PINS static content server. 
At the same time this servlet also can be used as a simple file reader.
