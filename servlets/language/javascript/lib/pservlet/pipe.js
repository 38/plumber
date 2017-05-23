// The pipe related operations
using("pservlet/blob");

pservlet.pipe = {
	/**
	 * The pipe options
	 **/
	flags: {
		INPUT:   __PIPE_INPUT,     /* Input pipe */
		OUTPUT:  __PIPE_OUTPUT,    /* Output pipe */
		ASYNC:   __PIPE_ASYNC,     /* Async write pipe (NOTE: this only valid for the output pipe) */
		SHADOW:  __PIPE_SHADOW,    /* Shadow pipe */
		PERSIST: __PIPE_PERSIST,   /* Persist pipe */
		DISABLED: __PIPE_DISABLED  /* Shadow disabled (NOTE: this only vlaid for the shadow pipe) */
	},
	/**
	 * Define a new pipe when the servlet is initialized
	 * @param name the name of the pipe
	 * @param flags the creation flag
	 * @param type_expr the type expression
	 * @return the pipe id
	 **/
	define: function (name, flags, type_expr) {
		return __define(name, flags, type_expr);
	},
	/**
	 * Read the data from the pipe and put it to the blob reader
	 * @param pipe the pipe id
	 * @param count number of bytes to read
	 * @return the blob reader contains the data
	 **/
	read: function(pipe, count) {
		var handle = __read(pipe, count);
		return pservlet.blob.makeBlobReader(handle);
	},
	/**
	 * Write the data to the pipe
	 * @param pipe the pipe id
	 * @param data the data object
	 * @param model if there's an model passed in, use the model to serialize the data
	 * @return the remaining data that is not read yet
	 */
	write: function(pipe, data, model) {
		if(!!model)
		{
			var buffer = model.dump(data);
			var size = model.model().getSize();
			while(size > 0) {
				var rc = __write(pipe, buffer);
				if(rc == 0) break;
				size -= rc;
				buffer = buffer.slice(rc);
			}
			return buffer;
		}
		else if(data instanceof ArrayBuffer)
		{
			var buffer = data;
			var size = data.byteLength;
			while(size > 0) {
				var rc = __write(pipe, buffer);
				if(rc == 0) break;
				size -= rc;
				buffer = buffer.slice(rc);
			}
			return buffer;
		}
		else
		{
			data = data.toString();
			var rc = __write(pipe, data);
			if(rc == data.length) 
				return "";
			return data.slice(rc);
		}
	},
	/**
	 * Check if the pipe has reached the end
	 * @param pipe the pipe id
	 * @return the check result
	 **/
	eof: function(pipe) {
		return __eof(pipe);
	},
	/**
	 * Set a pipe flags
	 * @param pipe the pipe id
	 * @param flag the flag to set
	 * @return nothing
	 **/
	set_flag: function(pipe, flag) {
		__set_flag(pipe, flag, true);
	},
	/**
	 * Clear a pipe flags
	 * @param pipe the pipe id
	 * @param flag the flag to clear
	 * @return nothing
	 **/
	clr_flag: function(pipe, flag) {
		__set_flag(pipe, flag, false);
	},
	/**
	 * Get flags 
	 * @param pipe the pipe id
	 * @return the flags
	 **/
	get_flags: function(pipe) {
		return __get_flags(pipe);
	},
	/**
	 * Push state to the pipe
	 * @param pipe the pipe id
	 * @param state the state object
	 * @return nothing
	 **/
	push_state: function(pipe, state) {
		var stateString = JSON.stringify(state);
		__push_state(pipe, stateString);
	},
	/**
	 * Pop state form the pipe
	 * @param pipe the pipe id
	 * @param state the state object
	 * @return the poped string
	 **/
	pop_state: function(pipe) {
		var stateString = __pop_state(pipe);
		if(stateString !== undefined)
			return JSON.parse(stateString);
	},
	Pipe: class {
		constructor(pid) {
			this._pipe_id = pid;
			this._flags = pservlet.pipe.get_flags(pipe);
			this._state_object = pservlet.pipe.pop_state(pid);	
		}
		getState() {
			return this._state_object;
		}
		setState(state) {
			this._state_object = state;
			pservlet.pipe.push_state(this._pipe_id, this._state_object);
		}
		eof() {
			return pservlet.pipe.eof(this._pipe_id);
		}
		/**
		 * read a string with maximum size of n
		 * @param n how many bytes to read
		 * @return the string
		 **/
		readString(n) {
			var blob = pservlet.pipe.read(this._pipe_id, n);
			return blob.readString(n);
		}

	}
};
