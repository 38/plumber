using("pservlet");
pservlet.setupCallbacks({
	init: function() {
		var context = {};
		context.input  = pservlet.pipe.define("input", pservlet.pipe.flags.INPUT);
		context.output = pservlet.pipe.define("output", pservlet.pipe.flags.OUTPUT);
		return context;
	},
	exec: function(context) {
		while(!pservlet.pipe.eof(context.input))
		{
			tmp = pservlet.pipe.read(context.input, 1024);
			pservlet.log.warning(tmp.size());
			if(tmp.size() == 0 && !pservlet.pipe.eof(context.input)) 
			{
				pservlet.pipe.set_flag(context.output, pservlet.pipe.flags.PERSIST);
				pservlet.pipe.set_flag(context.input, pservlet.pipe.flags.PERSIST);
				return;
			}
			pservlet.pipe.write(context.output, tmp.readBytes());
		}
		pservlet.log.info("connection should be closed");
		pservlet.pipe.clr_flag(context.output, pservlet.pipe.flags.PERSIST);
		pservlet.pipe.clr_flag(context.input, pservlet.pipe.flags.PERSIST);
	}
});
