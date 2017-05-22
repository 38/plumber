//Extract zip file information from input and write it to output as JSON format
//To run this demo, use command
//bin/pstest -p input=/usr/share/doc/texlive-doc/latex/latexcourse-rug/latexcourse-rug-sources.zip,output=/tmp/out.txt -s bin/lib javascript misc/jsdemo/readzip.js 
using("pservlet", "ctypes");

// Define the zip file type

var zip_file_signature = ctypes.modelOf({
	pk:   ctypes.fixed_length_string_t(2),
	type: ctypes.uint16_t()
});

var local_file_header = ctypes.modelOf({
	version: ctypes.uint16_t(),
	flags:   ctypes.uint16_t(),
	method:  ctypes.uint16_t(),
	timestamp: {
		time: ctypes.uint16_t(),
		date: ctypes.uint16_t()
	},
	crc32:   ctypes.uint32_t(),
	compressed_size: ctypes.uint32_t(),
	uncompressed_size: ctypes.uint32_t(),
	name_length: ctypes.uint16_t(),
	extra_length: ctypes.uint16_t()
});
pservlet.setupCallbacks({
	init : function (inputCount, outputCount) {
		var n = Number(inputCount);
		var ret = {
			input: pservlet.pipe.define("input", pservlet.pipe.flags.INPUT),
			output: pservlet.pipe.define("output", pservlet.pipe.flags.OUTPUT)
		};
		return ret;
	},
	exec : function (context) {
		
		var blob = pservlet.pipe.read(context.input, local_file_header.size() + zip_file_signature.size());
		var readBlob = function(n){
			return blob.readBytes(n);
		};

		var nfiles = 0, csize = 0, usize = 0;

		result = [];

		while(1) {
			var sig = zip_file_signature.parse(readBlob);
			if(sig.pk != "PK") break;
			if(sig.type == 0x0403) 
			{
				var header = local_file_header.parse(readBlob);

				blob = pservlet.pipe.read(context.input, header.name_length + header.compressed_size + header.extra_length);

				var var_header = ctypes.modelOf({
					filename: ctypes.fixed_length_string_t(header.name_length),
					extdata:  ctypes.arrayOf(ctypes.int8_t(), header.extra_length)
				}).parse(readBlob);

				blob = pservlet.pipe.read(context.input, local_file_header.size() + zip_file_signature.size());
				nfiles ++;
				csize += header.compressed_size;
				usize += header.uncompressed_size;

				result.push({
					signature: sig,
					vahead: var_header,
					header: header
				});
			}
		}
		
		pservlet.pipe.write(context.output, JSON.stringify(result));
		pservlet.log.notice("# of files", nfiles, "compressed size", csize, "uncompressed size", usize, "rate", csize / (usize * 1.0));
	}
});

