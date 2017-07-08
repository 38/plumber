"""
Copyright (C) 2017, Hao Hou

Example:

    tc = TypeContext()

    @tc.prototype("test", "pipe", "field")
    class ModelA(ModelBase):
        fieldA = Int8()
        class Child(ModelBase):
            fieldB = Int8()
            fieldC = Int32()
            fieldA = Uint32()

    inst = mc.init_instance()

    inst.test.fieldA = 3
    inst.test.Child.fieldC
    inst.test.Child.fieldB
    inst.test.Child.fieldA
"""
import pservlet
import types
import inspect

class Primitive(object):
    """
        Represents a primitive type
    """
    __is_type_model__ = True
    def read(self, instance, accessor):
        """
            Read the primitive data from the type instance with given accessor
        """
        raise NotImplemented
    def write(self, instance, accessor):
        """
            Write the primitve data from the type instance with given accessor
        """
        raise NotImplemented

def _create_value_accessor(type_obj, accessor):
    """
        Create a field accessor that defines the read and write to a initialized field
    """
    def _getter(self):
        return type_obj.read(self.instance, accessor)
    def _setter(self, val):
        type_obj.write(self.instance, accessor, val)
    return (_getter, _setter) 


def _get_type_model_obj():
    """
        Create a pstd type model object
    """
    return pservlet.TypeModel()

def _get_type_model_inst(model):
    """
        Create a pstd type instance object from the given model
    """
    return pservlet.TypeInstance(model)

def _get_accessor(type_model, pipe, field):
    """
        Create an accessor that can access the given pipe's given field with the specified type model
    """
    return type_model.accessor(pipe, field)

class _Instance(object):
    """
        The class used to access the typed data in the strong typed pipe system 
        This class is the the actual interface for value access. 
        For each servlet execution, the execute function should create an model
        instance by calling ModelCollection.init_instance()
    """
    def __init__(self, inst, types):
        self._inst = inst
        self._types = types
        self._type_inst = {}
    def __getattribute__(self, key):
        if key == "_types": 
            return object.__getattribute__(self, key)
        if key in self._types:
            if key not in self._type_inst:
                self._type_inst[key] = self._types[key](self._inst)
            return self._type_inst[key]
        else:
            return object.__getattribute__(self, key)

class ModelBase(object): 
    """
        The base class for the type model. We can define the type accessing model by inheriting this class with the decorator
        TypeContext.model_class
    """
    __children__ = []
    __primitives__ = {}
    def __init__(self, type_instance):
        self.instance = type_instance
        for name,child in self.__children__:
            child_inst = child(type_instance)
            setattr(self, name, child_inst)
    def __getattribute__(self, key):
        if key[:2] == "__" or key not in self.__primitives__:
            return object.__getattribute__(self, key)
        else:
            return self.__primitives__[key][0](self)
    def __setattr__(self, key, val):
        if key[:2] == "__" or key not in self.__primitives__:
            return object.__setattr__(self, key, val)
        else:
            return self.__primitives__[key][1](self, val)

class TypeContext(object):
    """
        The strong typed pipe context, which should be constructed in the initailization callback
        in the servlet
    """
    def __init__(self):
        self._model = _get_type_model_obj()
        self._types = {}
    def _add_model(self, name, pipe, field, model):
        def _patch_class(cls, prefix):
            cls.__children__ = []
            cls.__primitives__ = {}
            for name in dir(cls):
                obj = getattr(cls, name)
                if getattr(obj, "__is_type_model__", False):
                    accessor = _get_accessor(self._model, pipe, prefix + ("." if prefix else "") + name) 
                    cls.__primitives__[name] = _create_value_accessor(obj, accessor)
                elif inspect.isclass(obj) and issubclass(obj, ModelBase):
                    cls.__children__.append((name, obj))
                    setattr(cls, name, _patch_class(obj, prefix + ("." if prefix else "") + name))
            return cls
        self._types[name] = _patch_class(model, field)
    def model_class(self, name, pipe, field = ""):
        """
            The decorator used to define a model class
                name    The name for this model
                pipe    The pipe from which we read/write the data
                field   The field expression we want to access
        """
        def _decorator(cls):
            if issubclass(cls, ModelBase):
                self._add_model(name, pipe, field, cls)
            else:
                raise TypeError("The model class must be a subclass of ModelBase")
            return None
        return _decorator
    def init_instance(self):
        """
            Initialize a new type accessor instance, this should be used in the execute function
        """
        return _Instance(_get_type_model_inst(self._model), self._types)


def _define_int_primitive(size, signed):
    class IntField(Primitive):
        def __init__(self):
            self.size   = size
            self.signed = signed
        def read(self, instance, accessor):
            return instance.read_int(accessor, self.size, self.signed + 0)
        def write(self, instance, accessor, value):
            return instance.write_int(accessor, self.size, self.signed + 0, int(value))
    return IntField

def _define_float_primitive(size):
    class FloatField(Primitive):
        def __init__(self):
            self.size = size
        def read(self, instance, accessor):
            return instance.read_float(accessor, self.size)
        def write(self, instance, accessor, value):
            return instance.write_float(accessor, self.size, float(value))
    return FloatField

Int8  =  _define_int_primitive(1, True)
Int16 =  _define_int_primitive(2, True)
Int32 =  _define_int_primitive(4, True)
Int64 =  _define_int_primitive(8, True)
Uint8 =  _define_int_primitive(1, False)
Uint16 = _define_int_primitive(2, False)
Uint32 = _define_int_primitive(4, False)
Uint64 = _define_int_primitive(8, False)
Float  = _define_float_primitive(4)
Double = _define_float_primitive(8)

ScopeToken = _define_int_primitive(4, False)


