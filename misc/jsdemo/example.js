using("pservlet", "ctypes");

pservlet.setupCallbacks({
	init : function (inputCount, outputCount) {
		pservlet.log.debug("Hello Plumber", inputCount, outputCount, "Plumber Version", pservlet.version);
		var n = Number(inputCount);
		var ret = {
			inputs: [],
			outputs: []
		};
		for(var i = 0; i < n; i ++)
		{
			var pipeName = "input" + i;
			ret.inputs[i] = pservlet.pipe.define(pipeName, pservlet.pipe.flags.INPUT);
		}

		n = Number(outputCount);
		for(var i = 0; i < n; i ++)
		{
			ret.outputs.push(pservlet.pipe.define("output" + i, pservlet.pipe.flags.OUTPUT))
		}

		return ret;
	},
	exec : function (context) {
		for(var pipe in context) {
			pservlet.log.info(pipe+ "=" + context[pipe]);
		}
		var jpeg_header_model = ctypes.modelOf({
			soi_marker: ctypes.arrayOf(ctypes.uint8_t(), 2),
			app0: {
				marker: ctypes.uint16_t(),
				length: ctypes.uint16_t(),
				id:     ctypes.fixed_length_string_t(4),
				zero:   ctypes.uint8_t(),
				version:ctypes.uint16_t(),
				density:ctypes.uint8_t(),
				xdensity: ctypes.uint16_t(),
				ydensity: ctypes.uint16_t(),
				xthumbnail: ctypes.uint8_t(),
				ythumbnail: ctypes.uint8_t()
			}
		});
		
		var inp = __read(context.inputs[0], jpeg_header_model.size());
		var blob = pservlet.blob.makeBlobReader(inp);

		pservlet.log.warning("result", JSON.stringify(jpeg_header_model.parse(function(size) {
			return blob.readBytes(size);
		})));

		var test_model = ctypes.modelOf({
			x: ctypes.int8_t(),
			y: ctypes.int8_t(),
			z: ctypes.int8_t(),
			w: ctypes.int8_t(),
			p: ctypes.float_t(),
			n: ctypes.fixed_length_string_t(10)
		});

		var test = {
			x: 65,
			y: 66,
			z: 67,
			w: 68,
			p: 3.14,
			n: "WTF!!"
		};

		//var ab = test_model.dump(test);
		//__write(context.outputs[0], ab);
		pservlet.pipe.write(context.outputs[0], test, test_model);

	}
});

