// The binary blob

using("handle");

pservlet.blob = {
	/**
	 * The blob reader class, used to read a pooled bolb buffer provided by the servlet runtime
	 **/
	BlobReader: class extends handle.HandleBase {
		/**
		 * constructor
		 **/
		constructor() {
			super();
			this._offset = 0;   // The offset in the blob
			this._size   = __blob_size(this.getHandle()); // The size of this blob
		}

		/**
		 * Read bytes from the blob
		 * @param n how many bytes to read
		 * @return the array buffer that contains the data
		 **/
		readBytes(n) {
			if(n === undefined) n = 0;
			if(this._size <= this._offset + n) 
				n = this._size - this._offset;
			var ret = __blob_get(this.getHandle(), this._offset, n, 0);
			if(ret !== undefined) {
				//Copy the sentinel, so that we can make the reader of this buffer pervent GC dispose it
				ret.__sentinel = this.__sentinel;
				this._offset += ret.byteLength;
			}

			return ret;
		}

		/**
		 * Read a string of n bytes from the blob
		 * @param n how many bytes to read
		 * @return the result string 
		 **/
		readString(n) {
			if(n === undefined) n = 0;
			
			if(this._size <= this._offset + n)
				n = this._size - this._offset;

			var ret = __blob_get(this.getHandle(), this._offset, n, 1);
			if(ret !== undefined) {
				this._offset += ret.length;
			}

			return ret;
		}

		/**
		 * Skip n bytes in the blob buffer
		 * @param n how many btyes
		 * @return nothing
		 **/
		skip(n) {
			var offset = this._offset + n;
			if(offset > this._size) offset = this._size;

			this._offset = offset;
		}

		/**
		 * Reset the blob buffer and move the current location to 0
		 * @return nothing
		 **/
		reset() {
			this._offset = 0;			
		}

		/**
		 * Get the size of the blob
		 * @return nothing
		 **/
		size() {
			return __blob_size(this.getHandle());
		}

		/**
		 * Bytes avaliable
		 * @return the bytes that has not yet been read
		 **/
		bytesAvaliable() {
			return this.size() - this._offset;
		}

		/**
		 * Unread the bytes in the blob
		 * @param pipe the pipe to unread
		 * @param n the number of bytes to unread
		 * @return nothing
		 **/
		unread(pipe, n) {
			var handle = this.getHandle();
			var offset = this._offset - n;
			if(offset < 0) offset = 0;
			var bytes_to_unread = this.size() - offset;
			__unread(pipe, this.getHandle(), bytes_to_unread); 
		}
	},
	makeBlobReader: function (hid) {
		return handle.getHandleObject(pservlet.blob.BlobReader, hid);
	}
};
