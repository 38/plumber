/**
 * The C native type parser
 **/
const ctypes = function() {

	/**
	 * The base class for a value, which is the representation of a 
	 * C primitive
	 **/
    class Value {}

	/**
	 * A numeral value with default endian
	 **/
	class Num extends Value {
		/**
		 * @constructor
		 * @param typedBuffer the typed buffer should used to parse this type
		 **/
		constructor(readView, writeView, size) {
			super();
			this.__readView = readView;
			this.__writeView = writeView;
			this._size = size;
		}
		/**
		 * get the size of the value
		 **/
		getSize() {
			return this._size;
		}
		/**
		 * parse the value from the read callback
		 * @param read_callback the callback
		 **/
		parse(view, offset) {
			return this.__readView(view, offset);
		}
		/**
		 * dump a value to an array buffer
		 **/
		dump(value, view, offset) {
			return this.__writeView(value, view, offset);
		}
	}
	
	/**
	 * A string with fixed length
	 **/
	class FixedLengthString extends Value {
		/** 
		 * @constructor
		 * @size size of the string
		 * @wide if this is a wide string array
		 **/
		constructor(size, wide) {
			super();
			this._size = size;
			this._wide = !!wide;
		}
		/**
		 * Get the size of the type
		 **/
		getSize() {
			return this._size * (this._wide ? 2 : 1);
		}
		/**
		 * parse a value from the read_callback
		 * @param read_callback the callback
		 **/
		parse(view, offset) {
			var readResult = [];
			for(var i = 0; i < this._size; i ++)
				readResult.push(this._wide ? view.getInt16(offset + i * 2) : 
						                    view.getInt8(offset + i));
			return String.fromCharCode.apply(null, readResult);
		}
		/**
		 * dump a value to an array buffer
		 **/
		dump(value, view, offset) {
			if(typeof(value) !== "string") 
				throw new TypeError("The value must be a string");

			for(var i = 0; i < value.length && i < this._size; i ++)
				if(this._wide)
					view.setInt16(offset + i * 2, value.charCodeAt(i));
				else
					view.setInt8(offset + i, value.charCodeAt(i));

			for(var i = value.length; i < this._size; i ++)
				if(this._wide)
					view.setInt16(offset + i * 2, 0);
				else
					view.setInt8(offset + i, 0);
		}
	};

	/**
	 * An array of values 
	 **/
	class ValueArray {
		/**
		 * @constructor
		 * @param baseType base type
		 * @param size the number of elements in the array
		 **/
		constructor(baseType, size) {
			this._size = size;
			this._baseType = baseType;
		}
	};
	
	/**
	 * The model for the C native type, a model is a representation of a C type and
	 * can be used to parse the binary data to a JS object.
	 * Use modelOf function to create a new model 
	 **/
	class Model {}

	/**
	 * The constant value, which is not actually a part of data section,
	 * but some additional data should be added to the returned JS object
	 **/
	class ConstModel extends Model {
		/**
		 * @constructor
		 * @param value the value of the const
		 **/
		constructor(value) {
			super();
			this._value = value;
		}
		/**
		 * @brief get the size of the model
		 **/
		getSize() {
			return 0;
		}
		/**
		 * parse the value from the read callback
		 * @read_callback the read callback to use
		 **/
		parse(read_callback) { 
			return this._value; 
		}
		/**
		 * dump the object to an array buffer
		 * @param obj the object to dump
		 * @param buffer the buffer used to dump
		 **/
		dump(obj, buffer) {
		}
	}

	/**
	 * The model for a C primitive value
	 **/
	class ValueModel extends Model {
		/**
		 * @constructor
		 * @param value the value definition
		 **/
		constructor(value) {
			super();
			this._value = value;
		}
		/**
		 * get the size of the vlaue
		 **/
		getSize() { 
			return this._value.getSize(); 
		}
		/** 
		 * parse the value from the read callback
		 * @param read_callback the read callback to use
		 **/
		parse(view, offset) {
			return this._value.parse(view, offset);
		}
		/**
		 * dump the object to an array buffer
		 * @param obj the object to dump
		 * @param buffer the buffer used to dump
		 **/
		dump(obj, view, offset) {
			this._value.dump(obj, view, offset);
		}
	}

	/**
	 * The model for an array
	 **/
	class ArrayModel extends Model {
		/**
		 * @constructor
		 * @param baseModel the base model of the array
		 * @param size the size of the array
		 **/
		constructor(baseModel, size) {
			super();
			this._base = baseModel;
			this._size = size;
		}
		/**
		 * get the size of the value
		 **/
		getSize() { 
			return this._base.getSize() * this._size; 
		}
		/**
		 * parse the value from the read callback
		 * @param read_callback the callback to use
		 **/
		parse(view, offset) {
			var ret = [];
			for(var i = 0; i < this._size; i ++)
				ret.push(this._base.parse(view, offset + i * this._base.getSize()));
			return ret;
		}
		/**
		 * dump the object to an array buffer
		 * @param obj the object to dump
		 * @param buffer the buffer used to dump
		 **/
		dump(obj, view, offset) {
			for(var i = 0;  i < this._size; i ++) 
				this._base.dump(obj[i], view, offset + i * this._base.getSize());
		}
	}

	/**
	 * The model represents an object (on the other hand, a C struct)
	 **/
	class ObjectModel extends Model {
		/**
		 * @constructor
		 * @param children the dictionary that contains all its children
		 **/
		constructor(children) {
			super();
			this._children = children;
			this._size = 0;
			for(var childName in children)
				this._size += children[childName].getSize();
		}
		/**
		 * the size of the object
		 **/
		getSize() {
			return this._size;
		}
		/**
		 * parse from the read callback
		 * @param read_callback the callback to use
		 **/
		parse(view, offset) {
			var ret = {}
			for(var name in this._children)
			{
				ret[name] = this._children[name].parse(view, offset);
				offset += this._children[name].getSize();
			}
			return ret;
		}
		/**
		 * dump the object to an array buffer
		 * @param obj the object to dump
		 * @param buffer the buffer used to dump
		 **/
		dump(obj, view, offset) {
			for(var name in this._children)
			{
				this._children[name].dump(obj[name], view, offset);
				offset += this._children[name].getSize();
			}
		}
	}

	/**
	 * Get the model from a object definition
	 **/
	function _getModel(model) {
		if(model instanceof Value)
			return new ValueModel(model);
		else if(model instanceof ValueArray)
			return new ArrayModel(_getModel(model._baseType), model._size);
		else if(model instanceof Model)
			return model;
		else if(model instanceof Object)
		{
			var modelObject = {}
			for(var name in model) 
				modelObject[name] = _getModel(model[name]);
			return new ObjectModel(modelObject);
		}
		else return new ConstModel(model); 
	}
   
	function _get_int8(view, offest) { return view.getInt8(offest, true); }
	function _set_int8(value, view, offest) { return view.setInt8(offest, value, true); }
	function _get_int16(view, offest) { return view.getInt16(offest, true); }
	function _set_int16(value, view, offest) { return view.setInt16(offest, value, true); }
	function _get_int32(view, offest) { return view.getInt32(offest, true); }
	function _set_int32(value, view, offest) { return view.setInt32(offest, value, true); }
	function _get_uint8(view, offest) { return view.getUint8(offest, true); }
	function _set_uint8(value, view, offest) { return view.setUint8(offest, value, true); }
	function _get_uint16(view, offest) { return view.getUint16(offest, true); }
	function _set_uint16(value, view, offest) { return view.setUint16(offest, value, true); }
	function _get_uint32(view, offest) { return view.getUint32(offest, true); }
	function _set_uint32(value, view, offest) { return view.setUint32(offest, value, true); }
	function _get_float(view, offset) { return view.getFloat32(offset, value, true); }
	function _set_float(value, view, offset) { return view.setFloat32(offset, value, true); }
	function _get_double(view, offset) { return view.getFloat64(offset, value, true); }
	function _set_double(value, view, offset) { return view.setFloat64(offset, value, true); }
	return {
		int8_t:   function(){ return new Num(_get_int8,   _set_int8,   1); },
		uint8_t:  function(){ return new Num(_get_uint8,  _set_uint8,  1);},
		int16_t:  function(){ return new Num(_get_int16,  _set_int16,  2);},
		uint16_t: function(){ return new Num(_get_uint16, _set_uint16, 2);},
		int32_t:  function(){ return new Num(_get_int32,  _set_int32,  4);},
		uint32_t: function(){ return new Num(_get_uint32, _set_uint32, 4);},
		float_t:  function(){ return new Num(_get_float,  _set_float,  4);},
		double_t: function(){ return new Num(_get_double, _set_double, 8);},
		fixed_length_string_t: function(size) {
			return new FixedLengthString(size);
		},
		arrayOf:  function(type, size) {
			return new ValueArray(type, size); 
		},
		/**
		 * Get the model of a type.
		 * For example: for the C struct
		 * struct {
		 * 	int a;
		 * 	char b;
		 * 	int c[3];
		 * };
		 * We can use the following way to parse it:
		 * modelOf({
		 * 	a: int32_t(),
		 * 	b: int8_t(),
		 * 	c: arrayOf(int32_t(), 3)
		 * 	})
		 **/
		modelOf: function(type) {
			var model = _getModel(type);
			return {
				/**
				 * Get the size of the model
				 **/
				size: function () {
					return model.getSize();
				},
				/**
				 * Parse with given read callback
				 * @param read callback the read callback to use
				 **/
				parse: function(read_callback) {
					var size = model.getSize();
					var buffer = read_callback(size);
					var view = new DataView(buffer);
					return model.parse(view, 0);
				},
				/**
				 * get the model data
				 **/
				model: function () {
					return model;
				},
				/**
				 * dump the object to a newly created array buffer
				 * @param obj the object to dump
				 **/
				dump: function(obj) {
					var size = model.getSize();
					var arrayBuffer = new ArrayBuffer(size);
					var view = new DataView(arrayBuffer);
					model.dump(obj, view, 0);
					return arrayBuffer;
				}
			};
		}
    };
}();
