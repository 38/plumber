const using = function () {
	var imported = {};
	return function _using() {
		for(var i = 0; i < arguments.length; i ++) {
			var script = arguments[i] + ".js";
			if(imported[script]) {
				continue;
			} else {
				imported[script] = 1;
				try {
					__import(script);
				} catch (e) {
					imported[script] = 0;
					throw e;
				}
			}
		}
	}
}();

