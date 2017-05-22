/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The pointer table which used to manage C++ pointers for the Javascript code
 * @file javascript/include/ptrtab.hpp
 **/
#ifndef __JAVASCRIPT_PTRTAB_HPP__
#define __JAVASCRIPT_PTRTAB_HPP__

namespace Servlet {
	namespace ObjectPool{

		/**
		 * @brief The type code
		 **/
		enum TypeCode {
			TypeCode_Blob,
			TypeCode_NumTypes
		};

		template <typename T> struct GetTypeCode;
		template <int N> struct GetType;

		template <int N = 0>
		struct Destructor
		{
			static inline int dispose(int typecode, void* ptr)
			{
				if(N == typecode) delete (typename GetType<N>::Type*)ptr;
				else return Destructor<N + 1>::dispose(typecode, ptr);

				return 0;
			}
		};

		template <>
		struct Destructor<TypeCode_NumTypes>
		{
			static int dispose(int typecode, void* ptr)
			{
				(void)ptr;
				(void)typecode;
				ERROR_RETURN_LOG(int, "Unknown type code");
			}
		};

#define OBJECT_TYEP(type) \
		template <> \
		struct GetTypeCode<type> {\
			enum {\
				R = TypeCode_##type\
			};\
		};\
		template <>\
		struct GetType<TypeCode_##type> {\
			typedef type Type;\
		}

		OBJECT_TYEP(Blob);

		/**
		 * @brief the pointer table
		 **/
		class Pool{
			/**
			 * @brief the internal representation of pointer
			 **/
			struct _Pointer {
				union {
					void*     ptr;          /*!< the actal pointer */
					uint32_t  next_unused;  /*!< the next unused */
				};
				int       typecode;         /*!< the typecode for this pointer */
			};

			uint32_t _first_unused;         /*!< the unused slot list */
			_Pointer* _pointers;            /*!< the pointer array */
			uint32_t _capacity;
			int _resize();
			public:

			/**
			 * @brief represent a reference to the pooled object
			 **/
			template <typename T>
			class Pointer {
				uint32_t    _val;          /*!< the actual value */
				Pool*       _tab;          /*!< the parent pointer table */
				bool        _just_created; /*!< indicates this object is just created */
				public:

				/**
				 * @param tab the object pool
				 * @param val the object id
				 * @param created indicates if this pointer is referencing an object which is just created
				 **/
				Pointer(Pool* tab, uint32_t val, bool created = false) : _val(val), _tab(tab), _just_created(created) {}

				/**
				 * @brief for the just-created-object, the destructor dispose it automatically
				 **/
				~Pointer()
				{
					if(_just_created && dispose() == ERROR_CODE(int))
					    LOG_ERROR("Cannot dispose the object");
				}

				/**
				 * @brief preserve this object, which means do not dispose the underlying object when the reference dead
				 * @return nothing
				 **/
				void preserve()
				{
					_just_created = false;
				}

				T& operator *()
				{
					return *(T*)(_tab->_pointers[_val].ptr);
				}

				T* operator ->()
				{
					return (T*)(_tab->_pointers[_val].ptr);
				}

				int dispose()
				{
					return _tab->dispose_object(_val);
				}

				/**
				 * @brief check if this is a "null pointer"
				 * @return if the pointer is a null pointer
				 **/
				bool is_null() const
				{
					return _val == ERROR_CODE(uint32_t) || NULL == _tab;
				}

				/**
				 * @brief converting the pointer to an integer means we want the object id
				 * @return object id
				 **/
				operator int32_t() const
				{
					return (int32_t)_val;
				}

				static Pointer<T> null()
				{
					return Pointer<T>(NULL, ERROR_CODE(uint32_t), false);
				}
			};

			/**
			 * @brief get a existing pointer from it's Id
			 * @param id the pointer id
			 * @return the pointer to the object
			 **/
			template <typename T>
			Pointer<T> get(uint32_t id)
			{
				if(id >= _capacity) return Pointer<T>(NULL, ERROR_CODE(uint32_t));

				_Pointer& pointer = _pointers[id];

				if(GetTypeCode<T>::R != pointer.typecode)
				{
					LOG_ERROR("Type mismatch");
					return Pointer<T>::null();
				}

				return Pointer<T>(this, id);
			}

			/**
			 * @brief check if this object pool has been successfully initialized
			 * @return if the pool has been initialized successfully
			 **/
			bool check_initialized();

			Pool();


			~Pool();

			/**
			 * @brief create a new pointer
			 * @return status code
			 **/
			template <typename T>
			Pointer<T> create()
			{
				if(_first_unused == ERROR_CODE(uint32_t) && _resize() == ERROR_CODE(int))
				{
					LOG_ERROR("Cannot resize the pointer table");
					return Pointer<T>::null();
				}

				uint32_t ret = _first_unused;

				uint32_t next = _pointers[ret].next_unused;

				if(NULL == (_pointers[ret].ptr = new T()))
				{
					LOG_ERROR("Cannot allocate new pointer to table");
					return Pointer<T>::null();
				}
				_pointers[ret].typecode = GetTypeCode<T>::R;

				_first_unused = next;

				return Pointer<T>(this, ret, true);
			}

			/**
			 * @brief dispose a used object
			 * @param id the id to the object
			 * @return status code
			 **/
			int dispose_object(uint32_t id);
		};
	}
}

#endif /* __JAVASCRIPT_PTRTAB_HPP__ */
