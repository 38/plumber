pservlet.log = function () {
	var sp = "\t";
	function join(args, sp) {
		var ret = "";
		if(args.length == 0) return "";
		ret = args[0];

		for(var i = 1; i < args.length; i ++)
			ret += (sp + args[i]);

		return ret;		
	}
	function get_log_writter(level) {
		return function() {
			var msg = join(arguments, sp);
			__log(level, msg);
		}
	}
	var ret = {
		debug: get_log_writter(__DEBUG),
		trace: get_log_writter(__TRACE),
		info:  get_log_writter(__INFO),
		notice:get_log_writter(__NOTICE),
		warning: get_log_writter(__WARNING),
		error: get_log_writter(__ERROR),
		fatal: get_log_writter(__FATAL),
		setValueSeperator: function (_sp) {
			sp = _sp;
		}
	};
	return ret;
}();
