#ifndef _VALUE_HEADER
#define _VALUE_HEADER

#include "JetExceptions.h"
#include "JetInstructions.h"

#include <map>
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

#undef Yield

namespace Jet
{
	struct	Value;
	struct	Function;
	struct	Capture;
	class	JetObject;
	struct	Generator;
	class	JetContext;	
	class	GarbageCollector;

	/// <summary>
	/// 值的类型
	/// </summary>
	enum class ValueType:char
	{
		//keep all garbage collectable types towards the end after NativeFunction
		//this is used for the GC being able to tell what it is quickly
		Null = 0,
		Int,
		Real,
		NativeFunction,
		String,
		Object,
		Array,
		Function,
		Userdata,
		Capture,		//you will never see this in the VM
	};

	/// <summary>
	/// 各类型的名称
	/// </summary>
	static const char* ValueTypes[] = { "Null", "Int", "Real", "NativeFunction", "String", "Object", "Array", "Function", "Userdata" };
	
	/// <summary>
	/// GC值
	/// </summary>
	template<class t>
	struct GCVal
	{		
		bool		m_Mark;
		bool		m_Grey;
		ValueType	m_Type;
		int			m_RefCount;	//used for native functions
		t			m_Data;
		JetContext* m_Context = nullptr;
		GCVal() { }

		GCVal(t tt)
		{
			m_Data = tt;
		}
	};
	
	/// <summary>
	/// 字符串
	/// </summary>
	struct String
	{
		bool mark, grey;
		unsigned char refcount;

		unsigned int length;
		unsigned int hash;
		//string data would be stored after this point
	};
	typedef GCVal<char*> JetString;

	
	/// <summary>
	/// 数组的存储区
	/// </summary>
	typedef std::vector<Value> _JetArrayBacking;

	/// <summary>
	/// 数组
	/// </summary>
	struct JetArray
	{
		bool	m_Mark;
		bool	m_Grey;
		ValueType m_Type;
		unsigned char m_RefCount;		
		JetContext* m_Context;
		_JetArrayBacking m_Data;
	};
	//typedef GCVal<_JetArrayBacking> JetArray;
	
	/// <summary>
	/// 数组迭代器
	/// </summary>
	typedef _JetArrayBacking::iterator _JetArrayIterator;

	/// <summary>
	/// 用户自定义类型数据
	/// </summary>
	struct JetUserdata
	{
		bool		m_Mark;
		bool		m_Grey;
		ValueType	m_Type;
		unsigned char m_RefCount;
		void*		m_Data;
		JetObject*	m_Prototype;

		JetUserdata(void* d, JetObject* o)
		{
			m_Data = d;
			m_Prototype = o;
		}
	};
	

	/// <summary>
	/// 本地函数的函数指针
	/// </summary>
	typedef Value(*JetNativeFunc)(JetContext*,Value*, int);
	
	//each instruction can have a double, or two integers
	/// <summary>
	/// 虚拟机指令
	/// </summary>
	struct Instruction
	{
		//指令ID
		InstructionType m_Instruction;

		//指令数据
		union
		{
			struct
			{
				int				m_Value;
				union
				{
					int			m_Value2;
					Function*	m_Function;
					JetString*	m_StringLiteral ;
					const char* m_String;
				};
			};

			int64_t		m_IntValue;			//整数字面量
			double		m_RealValue;		//实数字面量
		};
	};

	/// <summary>
	/// 函数
	/// </summary>
	struct Function
	{
		~Function()
		{
			for (auto ii: this->m_Instructions)
				if (ii.m_Instruction != InstructionType::CLoad 
					&& ii.m_Instruction != InstructionType::CInit
					&& ii.m_Instruction != InstructionType::LoadFunction 
					&& ii.m_Instruction != InstructionType::LdStr 
					&& ii.m_Instruction != InstructionType::LdInt
					&& ii.m_Instruction != InstructionType::LdReal 
					&& ii.m_Instruction != InstructionType::Call
					&& ii.m_Instruction != InstructionType::Load
					&& ii.m_Instruction != InstructionType::Store
					&& ii.m_Instruction != InstructionType::CStore)
					delete[] ii.m_String;
		}

		//参数数量
		unsigned int m_Args;
		//局部变量数量
		unsigned int m_Locals;
		//UpValue数量
		unsigned int m_UpValues;
		//是否是变参函数
		bool		m_VarArg; 
		//是否是生成器函数
		bool		m_Generator;
		//函数所属的Context
		JetContext* m_Context;		
		//函数的指令集
		std::vector<Instruction>	m_Instructions;

		//代码中的函数名
		std::string					m_Name;
	
		//调试信息
		struct DebugInfo
		{
			unsigned int	code;
			std::string		file;
			unsigned int	line;
		};
		std::vector<DebugInfo>		debuginfo;		//instruction->line number mappings
		std::vector<std::string>	debuglocal;		//local variable debug info
		std::vector<std::string>	debugcapture;	//capture variable debug info
	};

	/// <summary>
	/// 闭包
	/// </summary>
	struct Closure
	{
		bool m_Mark;
		bool m_Grey;
		Jet::ValueType	m_Type;
		unsigned char	m_RefCount;

		Function*		m_Prototype;//details about the function
		Generator*		m_Generator;

		unsigned char	m_UpValueCount;
		Capture**		m_UpValues;//captured values
	
		Closure*		m_Prev;//parent closure, used for searching for captures
	};

	/// <summary>
	/// 值类型
	/// </summary>
	struct Value
	{
		ValueType				m_Type;
		union
		{
			int64_t				m_IntValue;
			double				m_RealValue;
			
			struct
			{
				union
				{
					JetString*		m_String;
					JetObject*		m_Object;
					JetArray*		m_Array;
					JetUserdata*	m_UserData;
					Closure*		m_Function;	//jet function
				};
				union
				{
					unsigned int	m_Length;	//used for strings
				};
			};

			JetNativeFunc			m_NativeFunction;		//native func
		};

		Value();

		Value(JetString* str);
		Value(JetObject* obj);
		Value(JetArray* arr);
		
		Value(double val);
		Value(int val);
		Value(int64_t val);

		Value(JetNativeFunc a);
		Value(Closure* func);

		inline void	SetBool(bool v)
		{
			m_Type = ValueType::Int;
			m_IntValue = v ? 1 : 0;
		}

		explicit Value(JetUserdata* userdata, JetObject* prototype);

		Value& operator= (const JetNativeFunc& func)
		{
			return *this = Value(func);
		}

		void SetPrototype(JetObject* obj);

		std::string ToString(int depth = 0) const;

		template<class T>
		inline T*& GetUserdata()
		{
			return (T*&)this->m_UserData->m_Data;
		}

		const char* Type() const
		{
			return ValueTypes[(int)this->m_Type];
		}

		operator int()
		{
			if (m_Type == ValueType::Int)		return (int)m_IntValue;
			else if (m_Type == ValueType::Real)	return (int)m_RealValue;
			else if (m_Type == ValueType::String) return atoi(m_String->m_Data);
			throw RuntimeException("Cannot convert type " + (std::string)ValueTypes[(int)this->m_Type] + " to int!");
		}

		operator int64_t()
		{
			if (m_Type == ValueType::Int)		return m_IntValue;
			if (m_Type == ValueType::Real)	return (int64_t)m_RealValue;
			else if (m_Type == ValueType::String) return _atoi64(m_String->m_Data);
			throw RuntimeException("Cannot convert type " + (std::string)ValueTypes[(int)this->m_Type] + " to int!");
		}

		operator double()
		{
			if (m_Type == ValueType::Int)		return (double)m_IntValue;
			if (m_Type == ValueType::Real)	return m_RealValue;
			else if (m_Type == ValueType::String) return atof(m_String->m_Data);

			throw RuntimeException("Cannot convert type " + (std::string)ValueTypes[(int)this->m_Type] + " to real!");
		}
		
		Value operator() (JetContext* context, Value* v = 0, int args = 0);
		Value Call(Value* v = 0, int args = 0);//not recommended, but works for non native functions

		inline bool IsGenerator()
		{
			if (this->m_Type == ValueType::Function)
				return this->m_Function->m_Prototype->m_Generator;
			return false;
		}

		JetObject* GetPrototype();

		//reference counting stuff
		void AddRef();
		void Release();

		//this massively redundant case is only here because
		//c++ operator overloading resolution is dumb
		//and wants to do integer[pointer-to-object]
		//rather than value[(implicit value)const char*]
		Value& operator[] (int64_t key);
		Value& operator[] (const char* key);
		Value& operator[] (const Value& key);

		//binary operators
		bool operator==(const Value& other) const;

		//比较两个值,小于返回负数，等于返回0，大于返回正数
		int64_t	Compare(const Value& o);

		Value operator+( const Value &other );
		Value operator-( const Value &other );

		Value operator*( const Value &other );
		Value operator/( const Value &other );

		Value operator%( const Value &other );

		Value operator|( const Value &other );
		Value operator&( const Value &other );
		Value operator^( const Value &other );

		Value operator<<( const Value &other );
		Value operator>>( const Value &other );

		void operator+=(const Value &other);
		void operator-=(const Value &other);
		void operator*=(const Value &other);
		void operator/=(const Value &other);
		void operator%=(const Value &other);
		void operator|=(const Value &other);
		void operator&=(const Value &other);
		void operator^=(const Value &other);
		void operator<<=(const Value &other);
		void operator>>=(const Value &other);

		//unary operators
		Value operator~();
		Value operator-();

		// -
		void	Negate();
		// ++
		void	Increase();
		// --
		void	Decrease();

		//空值
		static Value	Empty;
		static Value	Zero;
		static Value	One;
	private:
		Value CallMetamethod(const char* name, const Value* other);
		Value CallMetamethod(JetObject* table, const char* name, const Value* other);

		bool TryCallMetamethod(const char* name, const Value* args, int numargs, Value* out) const;

		friend class JetContext;
	};

	// 使用宏定义将值设置为bool值，可以减少一次函数调用
#define set_value_bool(m_Value,b)	m_Value.m_Type=ValueType::Int;m_Value.m_IntValue=b?1:0;
#define is_value_primitive(a) (a.m_Type== ValueType::Int || a.m_Type== ValueType::Real)

	//a 和 b执行指定的运算，如加法运算为:VALUES_OP(a,b,+,+=);
#define VALUES_OP(a,b,op,op2) \
		{\
		switch (a.m_Type)	{\
			case ValueType::Int:	{	\
				if (b.m_Type == ValueType::Real)		{	a.m_Type = ValueType::Real;	a.m_RealValue = (double)a.m_IntValue op b.m_RealValue;	}\
								else if (b.m_Type == ValueType::Int)	{	a.m_IntValue op2 b.m_IntValue;}\
								else									{	a op2 b;	}\
				break;				}\
			case ValueType::Real:	{\
				if (b.m_Type == ValueType::Real)		{	a.m_RealValue op2 b.m_RealValue;		}\
								else if (b.m_Type == ValueType::Int)	{	a.m_RealValue op2 (double)b.m_RealValue;	}\
								else									{	a op2 b;	}\
				break;				}\
			default:a op2 b; break;\
			}\
		}

	//比较a和b的值，将结果存入r，如小于比较为:VALUES_CMP(a,a,b,<);
#define VALUES_CMP(r,a,b,op) \
			{\
			switch (a.m_Type)	{\
			case ValueType::Int:	{	\
				if (b.m_Type == ValueType::Real)		{	r.m_IntValue = (int)(((double)a.m_IntValue - b.m_RealValue) op 0);	}\
				else if (b.m_Type == ValueType::Int)	{	r.m_IntValue=(int)((a.m_IntValue - b.m_IntValue) op 0);}\
				else									{	r.m_IntValue=(int)(a.Compare(b) op 0);	}\
				break;				}\
			case ValueType::Real:	{\
				if (b.m_Type == ValueType::Real)		{	r.m_IntValue=(int)((a.m_RealValue - b.m_RealValue) op 0);		}\
				else if (b.m_Type == ValueType::Int)	{	r.m_IntValue=(int)((a.m_RealValue - (double)b.m_RealValue) op 0);	}\
				else									{	r.m_IntValue=(int)(a.Compare(b) op 0);	}\
				break;				}\
			default:r.m_IntValue=(int)(a.Compare(b) op 0); break;\
			}\
			r.m_Type=ValueType::Int;\
			}

	/// <summary>
	/// 闭包的捕获值
	/// </summary>
	struct Capture
	{
		bool m_Mark;
		bool m_Grey;
		Jet::ValueType	m_Type;
		unsigned char	m_RefCount;

		Value*			m_Ptr;//points to self when closed, or stack when open
		Value			m_Value;
		bool			m_Closed;

#ifdef _DEBUG
		int			m_UseCount;
		Closure*	m_Owner;
#endif
	};

	/// <summary>
	/// 对象节点，用于在对象内部实现Map
	/// </summary>
	struct ObjNode
	{
		// Key
		Value		first;
		// Value
		Value		second;
		// Next
		ObjNode*	next;
	};

	/// <summary>
	/// 对象内部元素的迭代器
	/// </summary>
	template <class T> class ObjIterator
	{
		typedef ObjNode Node;
		typedef ObjIterator<T> Iterator;
		Node*		ptr;
		JetObject*	parent;
	public:
		ObjIterator()
		{
			this->parent = 0;
			this->ptr = 0;
		}

		ObjIterator(JetObject* p)
		{
			this->parent = p;
			this->ptr = 0;
		}

		ObjIterator(JetObject* p, Node* node)
		{
			this->parent = p;
			this->ptr = node;
		}

		bool operator==(const Iterator& other)
		{
			return ptr == other.ptr;
		}

		bool operator!=(const Iterator& other)
		{
			return this->ptr != other.ptr;
		}

		Iterator& operator++()
		{
			if (ptr && ((this->ptr-this->parent->m_Nodes) < ((int)this->parent->m_NodeCount-1)))
			{
				do
				{
					this->ptr++;
					if (ptr->first.m_Type != ValueType::Null)
						return *this;
				}
				while ((this->ptr-this->parent->m_Nodes) < ((int)this->parent->m_NodeCount-1));
			}
			this->ptr = 0;

			return *this;
		};

		Node*& operator->() const
		{	// return pointer to class object
			return (Node*&)this->ptr;//this does pointer magic and gives a reference to the pair containing the key and value
		}

		Node& operator*()
		{
			return *this->ptr;
		}
	};

	/// <summary>
	/// 脚本对象
	/// </summary>
	class JetObject
	{
		friend class ObjIterator<Value>;
		friend struct Value;
		friend class GarbageCollector;
		friend class JetContext;

		//gc header
		bool	m_Mark;
		bool	m_Grey;
		Jet::ValueType	m_Type;
		unsigned char	m_RefCount;

		JetContext*		m_Context;
		ObjNode*		m_Nodes;
		JetObject*		m_Prototype;

		unsigned int	m_Size;
		unsigned int	m_NodeCount;
	public:
		typedef ObjIterator<Value> Iterator;

		JetObject(JetContext* context);
		~JetObject();

		std::size_t key(const Value* v) const;

		Iterator find(const Value& key)
		{
			ObjNode* node = this->findNode(&key);
			return Iterator(this, node);
		}

		Iterator find(const char* key)
		{
			ObjNode* node = this->findNode(key);
			return Iterator(this, node);
		}

		//this are faster versions used in the VM
		Value get(const Value& key)
		{
			auto node = this->findNode(&key);
			return node ? node->second : Value();
		}
		Value get(const char* key)
		{
			auto node = this->findNode(key);
			return node ? node->second : Value();
		}

		//just looks for a node
		ObjNode* findNode(const Value* key);
		ObjNode* findNode(const char* key);

		//finds node for key or creates one if doesnt exist
		ObjNode* getNode(const Value* key);
		ObjNode* getNode(const char* key);

		//try not to use these in the vm
		Value& operator [](const Value& key);
		Value& operator [](const char* key);//special operator for strings to deal with insertions
		
		Iterator end()
		{
			return Iterator(this);
		}

		Iterator begin()
		{
			for (unsigned int i = 0; i < this->m_NodeCount; i++)
				if (this->m_Nodes[i].first.m_Type != ValueType::Null)
					return Iterator(this, &m_Nodes[i]);
			return end();
		}

		inline size_t size()
		{
			return this->m_Size;
		}

		inline void SetPrototype(JetObject* obj)
		{
			this->m_Prototype = obj;
		}

		void DebugPrint();

	private:
		//finds an open node in the table for inserting
		ObjNode* getFreePosition();

		//increases the size to fit new keys
		void resize();

		//memory barrier
		void Barrier();
	};

	/// <summary>
	/// 对Value的应用,类似于unique_ptr
	/// </summary>
	class ValueRef
	{
		Value m_Value;
	public:
		ValueRef(const Value& value)
		{
			this->m_Value = value;
			this->m_Value.AddRef();
		}

		ValueRef(ValueRef&& other)
		{
			this->m_Value = other.m_Value;
			other.m_Value = Value();
		}

		~ValueRef()
		{
			this->m_Value.Release();
		}

		inline operator Value()
		{
			return this->m_Value;
		}

		inline Value* operator ->()
		{
			return &this->m_Value;
		}

		inline Value& operator [](const char* c)
		{
			return this->m_Value[c];
		}

		inline Value& operator [](int i)
		{
			return this->m_Value[i];
		}

		inline Value& operator [](const Value& c)
		{
			return this->m_Value[c];
		}
	};

	/// <summary>
	/// 生成器
	/// </summary>
	struct Generator
	{
		enum class GeneratorState
		{
			Running,
			Suspended,
			Dead,
		};

		Generator(JetContext* context, Closure* closure, unsigned int args);

		void Yield(JetContext* context, unsigned int iptr);

		unsigned int Resume(JetContext* context);

		void Kill()//what happens when you return
		{
			this->m_State = GeneratorState::Dead;
			//restore iptr
		}

		int				m_CurrentIPtr;
		GeneratorState	m_State;
		Closure*		m_Closure;
		Value*			m_Stack;
		Value			m_LastYielded;	//used for acting like an iterator
	};
}

#endif