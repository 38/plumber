"use strict";

/**
 * A disposible handle, which is used to bind an servlet source Id in the servlet's object pool
 **/
const handle = {
	/**
	 * Get a new "disposible" class from the provided base type
	 * @param baseType the base type
	 * @param handle the handle to attach
	 * @return the newly created class
	 **/
	getHandleClass: function(baseType, handle) {
		return class extends baseType {
			constructor() {
				super();
			}
			getHandle() {
				return handle;
			}
		}
	},
	/**
	 * Like the getHandleClass, but instead of creates a class first, return an object
	 * directly
	 * @param baseType the base type
	 * @param hid the handle id
	 * @return the object
	 **/
	getHandleObject: function(baseType, hid) {
		var handleClass = handle.getHandleClass(baseType, hid);
		var ret = new handleClass();
		
		return ret;
	},
	/**
	 * the Handle's base class, which means all the handle wrapper should extend this class
	 **/
	HandleBase: class {
		/**
		 * @constructor
		 **/
		constructor() {
			var handle = this.getHandle();
			this.__sentinel = __sentinel(function() {
				__handle_dispose(handle);
			});
		}
	}
};
