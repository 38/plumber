pservlet = function () {
	function invokeIfDefined(func, defVal, wrapper) {
		if(func === undefined) 
			return function () {
				return defVal;
			}

		return wrapper;
	}
	var ret = {
		setupCallbacks: function (callbacks) {
			__servlet_def__ = {
				init : invokeIfDefined(callbacks.init, "", function () {
					var args = [];
					for(var i = 0; i < arguments.length; i ++)
						args.push(arguments[i]);
					return JSON.stringify(callbacks.init.apply(undefined, args));
				}),
				exec : invokeIfDefined(callbacks.exec, 0, function (context) {
					return callbacks.exec(JSON.parse(context));
				}),
				unload : invokeIfDefined(callbacks.unload, 0, function (context) {
					return callbacks.unload(JSON.parse(context));
				})
			};
		},
		version: __PLUMBER_RUNTIME_VERSION
	};
	return ret;
}();

using("pservlet/log", "pservlet/pipe", "pservlet/blob");
