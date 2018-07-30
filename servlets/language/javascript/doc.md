# language/javascript

## Description

The javascript servlet loader. 
This is the servlet which manage a Google V8 Javascript engine which can be used for Javascript servlets.

The threading model of this servlet is quite simple: for each worker thread, it owns a seperate Javascript interpreter.
So the servlet allows multiple Javascript function invocation by seperate worker thread.

The Plumber API language binding is provided by the servlet as well, by importing  the `pservlet` library with following code,
the javascript  code can define a servlet as following.

```javascript
using("pservlet");

pservlet.setupCallbacks({
	"init": function(/* servlet init param */) {
		// the servlet init code for example
	},
	"exec": function(context) {
		// the exec code
	}
});
```

We don't actually have a `cleanup` callback, because Javascript is a garbage collecting language.

## Ports

Any ports and type trait this servlet can have. It depends on how the servlet has been initialized and how the guest code looks like.

## Options

```
language/javascript <js-script-file> <js-param1> ... <js-paramN>
```

## Note

This is just a overview of javascript support component of Plumber. 

### Limit

Currently, the javascript API binding  doesn't provided a libproto interface for guest code. 
So it's really hard to use the servlet create typed component. 

Currently, we are planning to provide full support of the libproto support for javascript.
