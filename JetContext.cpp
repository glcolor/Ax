#include "JetContext.h"
#include "UniquePtr.h"

#include <stack>
#include <fstream>
#include <memory>

#undef Yield

using namespace Jet;

#define JET_BAD_INSTRUCTION 123456789

//----------------------------------------------------------------
Jet::MemoryAlloc Jet::g_MemoryAlloc = malloc;
Jet::MemoryFree Jet::g_MemoryFree = free;
//----------------------------------------------------------------

Value Jet::gc(JetContext* context,Value* args, int numargs) 
{ 
	context->RunGC();
	return Value::Empty;
}


Value Jet::tostring(JetContext* context, Value* args, int numargs)
{
	if (numargs >= 1)
	{
		auto str = context->CreateNewString(args->ToString().c_str(), true);
		return str;
	}
	throw RuntimeException("Invalid tostring call");
}


Jet::Value Jet::toint(JetContext* context, Value* args, int numargs)
{
	if (numargs >= 1)
	{
		return Value((int64_t)*args);
	}
	throw RuntimeException("Invalid int call");
}


Jet::Value Jet::toreal(JetContext* context, Value* args, int numargs)
{
	if (numargs >= 1)
	{
		return Value((double)*args);
	}
	throw RuntimeException("Invalid real call");
}

Value JetContext::Callstack(JetContext* context, Value* args, int numargs)
{
	context->StackTrace(JET_BAD_INSTRUCTION, 0);
	return Value::Empty;
}

Value Jet::print(JetContext* context,Value* args, int numargs) 
{ 
	auto of = context->GetOutputFunction();
	for (int i = 0; i < numargs; i++)
	{
		of("%s", args[i].ToString().c_str());
	}
	of("\n");
	return Value::Empty;
};

Value& JetContext::operator[](const std::string& id)
{
	auto iter = m_VariableIndex.find(id);
	if (iter == m_VariableIndex.end())
	{
		//add it
		m_VariableIndex[id] = (unsigned int)m_VariableIndex.size();
		m_Variables.push_back(Value::Empty);
		return m_Variables[m_VariableIndex[id]];
	}
	else
	{
		return m_Variables[(*iter).second];
	}
}

Value JetContext::Get(const std::string& name)
{
	auto iter = m_VariableIndex.find(name);
	if (iter == m_VariableIndex.end())
	{
		return Value::Empty;//return null
	}
	else
	{
		return m_Variables[(*iter).second];
	}
}

void JetContext::Set(const std::string& name, const Value& value)
{
	auto iter = m_VariableIndex.find(name);
	if (iter == m_VariableIndex.end())
	{
		//add it
		m_VariableIndex[name] = (unsigned int)m_VariableIndex.size();
		m_Variables.push_back(value);
	}
	else
	{
		m_Variables[(*iter).second] = value;
	}
}

Value JetContext::CreateNewObject()
{
	auto v = this->m_GC.New<JetObject>(this);
	v->m_RefCount = 0;
	v->m_Type = ValueType::Object;
	v->m_Grey = v->m_Mark = false;

	return Value(v);
}

Value JetContext::AddPrototype(const char* Typename)
{
	auto v = new JetObject(this);//this->m_GC.New<JetObject>(this);//auto v = new JetObject(this);
	v->m_RefCount = 0;
	v->m_Type = ValueType::Object;
	v->m_Grey = v->m_Mark = false;
	this->m_Prototypes.push_back(v);
	return v;
}

Value JetContext::CreateNewArray()
{
	auto a = m_GC.New<JetArray>();//new JetArray;
	a->m_RefCount = 0;
	a->m_Grey = a->m_Mark = false;
	a->m_Type = ValueType::Array;
	a->m_Context = this;

	return Value(a);
}

Value JetContext::CreateNewUserData(void* data, const Value& proto)
{
	if (proto.m_Type != ValueType::Object)
		throw RuntimeException("NewUserdata: Prototype supplied was not of the type 'object'\n");

	auto ud = m_GC.New<JetUserdata>(data, proto.m_Object);
	ud->m_Grey = ud->m_Mark = false;
	ud->m_RefCount = 0;
	ud->m_Type = ValueType::Userdata;
	return Value(ud, proto.m_Object);
}

Value JetContext::CreateNewString(const char* string, bool copy)
{
	if (copy)
	{
		size_t len = strlen(string);
		auto temp = new char[len+1];
		memcpy(temp, string, len);
		temp[len] = 0;
		string = temp;
	}
	auto str = m_GC.New<GCVal<char*>>((char*)string);
	str->m_Grey = str->m_Mark = false;
	str->m_RefCount = 0;
	str->m_Type = ValueType::String;
	str->m_Context = this;
	return Value(str);
}

#include "Libraries/File.h"
#include "Libraries/Math.h"

JetContext::JetContext() : m_GC(this), m_Stack(4096), m_CallStack(JET_MAX_CALLDEPTH, "Call Stack Overflow")
{
	this->m_SP = this->m_LocalStack;//initialize stack pointer
	this->m_CurFrame = 0;

	//add more functions and junk
	(*this)["print"] = print;
	(*this)["gc"] = ::gc;
	(*this)["callstack"] = JetContext::Callstack;
	(*this)["tostring"] = ::tostring;
	(*this)["string"] = ::tostring;
	(*this)["int"] = ::toint;
	(*this)["real"] = ::toreal;
	(*this)["pcall"] = [](JetContext* context, Value* args, int argc)
	{
		if (argc == 0)
			throw RuntimeException("Invalid argument count to pcall!");
		try
		{
			if (argc > 1)
				return context->Call(args, &args[1], argc-1);
			else if (argc == 1)
				return context->Call(args);
		}
		catch(RuntimeException e)
		{
			context->m_OutputFunction("PCall got exception: %s", e.reason.c_str());
			return Value::Zero;
		}
		return Value::Zero;
	};
	(*this)["error"] = [](JetContext* context, Value* args, int argc)
	{
		if (argc > 0)
			throw RuntimeException(args->ToString());
		else
			throw RuntimeException("User Error Thrown!");
		return Value::Empty;
	};

	(*this)["loadstring"] = [](JetContext* context, Value* args, int argc)
	{
		if (argc < 1 || args[0].m_Type != ValueType::String)
			throw RuntimeException("Cannot load non string");

		return context->Assemble(context->Compile(args[0].m_String->m_Data, "loadstring"));
	};

	(*this)["setprototype"] = [](JetContext* context, Value* v, int args)
	{
		if (args != 2)
			throw RuntimeException("Invalid Call, Improper Arguments!");

		if (v->m_Type == ValueType::Object && v[1].m_Type == ValueType::Object)
		{
			Value val = v[0];
			val.m_Object->m_Prototype = v[1].m_Object;
			return val;
		}
		else
		{
			throw RuntimeException("Improper arguments!");
		}
	};

	(*this)["require"] = [](JetContext* context, Value* v, int args)
	{
		if (args != 1 || v->m_Type != ValueType::String)
			throw RuntimeException("Invalid Call, Improper Arguments!");

		auto iter = context->m_RequireCache.find(v->m_String->m_Data);
		if (iter == context->m_RequireCache.end())
		{
			//check from list of libraries
			auto lib = context->m_Libraries.find(v->m_String->m_Data);
			if (lib != context->m_Libraries.end())
				return lib->second;

			//else load from file
			std::ifstream t(v->m_String->m_Data, std::ios::in | std::ios::binary);
			if (t)
			{
				int length;
				t.seekg(0, std::ios::end);    // go to the end
				length = (int)t.tellg();           // report location (this is the length)
				t.seekg(0, std::ios::beg);    // go back to the beginning
				UniquePtr<char[]> buffer(new char[length+1]);    // allocate memory for a buffer of appropriate dimension
				t.read(buffer, length);       // read the whole file into the buffer
				buffer[length] = 0;
				t.close();

				auto out = context->Compile(buffer, v->m_String->m_Data);
				auto fun = context->Assemble(out);
				auto temp = context->CreateNewObject();
				context->m_RequireCache[v->m_String->m_Data] = temp;
				auto obj = context->Call(&fun);
				if (obj.m_Type == ValueType::Object)
				{
					for (auto ii: *obj.m_Object)//copy stuff into the temporary object
						temp[ii.first] = ii.second;

					return temp;
				}
				else
				{
					context->m_RequireCache[v->m_String->m_Data] = obj;//just use what was returned
					return obj;
				}
			}
			else
			{
				throw RuntimeException("Require could not find include: '" + (std::string)v->m_String->m_Data + "'");
			}
		}
		else
		{
			return Value(iter->second);
		}
	};

	(*this)["getprototype"] = [](JetContext* context, Value* v, int args)
	{
		if (args == 1 && (v->m_Type == ValueType::Object || v->m_Type == ValueType::Userdata))
			return Value(v->GetPrototype());
		else
			throw RuntimeException("getprototype expected an object or userdata value!");
	};


	//setup the string and array tables
	this->m_StringPrototype = new JetObject(this);
	this->m_StringPrototype->m_Prototype = 0;
	(*this->m_StringPrototype)["append"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 2 && v[0].m_Type == ValueType::String && v[1].m_Type == ValueType::String)
		{
			size_t len = v[0].m_Length + v[1].m_Length + 1;
			char* text = new char[len];
			memcpy(text, v[0].m_String->m_Data, v[0].m_Length);
			memcpy(text+v[0].m_Length, v[1].m_String->m_Data, v[1].m_Length);
			text[len-1] = 0;
			return context->CreateNewString(text, false);
		}
		else
			throw RuntimeException("bad append call!");
	});
	(*this->m_StringPrototype)["lower"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args && v->m_Type == ValueType::String)
		{
			char* str = new char[v->m_Length+1];
			memcpy(str, v->m_String->m_Data, v->m_Length);
			for (unsigned int i = 0; i < v->m_Length; i++)
				str[i] = tolower(str[i]);
			str[v->m_Length] = 0;
			return context->CreateNewString(str, false);
		}
		throw RuntimeException("bad lower call");
	});
	(*this->m_StringPrototype)["upper"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args && v->m_Type == ValueType::String)
		{
			char* str = new char[v->m_Length+1];
			memcpy(str, v->m_String->m_Data, v->m_Length);
			for (unsigned int i = 0; i < v->m_Length; i++)
				str[i] = toupper(str[i]);
			str[v->m_Length] = 0;
			return context->CreateNewString(str, false);
		}
		throw RuntimeException("bad upper call");
	});
	//figure out how to get this working with strings
	(*this->m_StringPrototype)["_add"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 2 && v[0].m_Type == ValueType::String && v[1].m_Type == ValueType::String)
		{
			size_t len = v[0].m_Length + v[1].m_Length + 1;
			char* text = new char[len];
			memcpy(text, v[1].m_String->m_Data, v[1].m_Length);
			memcpy(text+v[1].m_Length, v[0].m_String->m_Data, v[0].m_Length);
			text[len-1] = 0;
			return context->CreateNewString(text, false);
		}
		else
			throw RuntimeException("bad string::append() call!");
	});
	(*this->m_StringPrototype)["length"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 1 && v->m_Type == ValueType::String)
			return Value((int64_t)v->m_Length);
		else
			throw RuntimeException("bad string:length() call!");
	});
	(*this->m_StringPrototype)["sub"] = [](JetContext* context, Value* v, int args)
	{
		if (args == 2)
		{
			if (v[0].m_Type != ValueType::String)
				throw RuntimeException("must be a string");

			int len = v[0].m_Length-(int)v[1];
			if (len < 0)
				throw RuntimeException("Invalid string index");

			char* str = new char[len+1];
			CopySizedString(str, &v[0].m_String->m_Data[(int)v[1]], len+1,len);
			str[len] = 0;
			return context->CreateNewString(str, false);
		}
		else if (args == 3)
		{
			throw RuntimeException("Not Implemented!");
		}
		else
			throw RuntimeException("bad sub call");
	};


	this->m_ArrayPrototype = new JetObject(this);
	this->m_ArrayPrototype->m_Prototype = 0;
	(*this->m_ArrayPrototype)["add"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 2)
			v->m_Array->m_Data.push_back(v[1]);
		else
			throw RuntimeException("Invalid add call!!");
		return Value::Empty;
	});
	(*this->m_ArrayPrototype)["size"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 1)
			return Value((int64_t)v->m_Array->m_Data.size());
		else
			throw RuntimeException("Invalid size call!!");
	});
	(*this->m_ArrayPrototype)["resize"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 2)
			v->m_Array->m_Data.resize((int)v[1]);
		else
			throw RuntimeException("Invalid resize call!!");
		return Value::Empty;
	});
	(*this->m_ArrayPrototype)["remove"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 2)
			v->m_Array->m_Data.erase(v->m_Array->m_Data.begin()+(int)v[1]);
		else
			throw RuntimeException("Invalid remove call!!");
		return Value::Empty;
	});

	struct arrayiter
	{
		JetArray* container;
		Value current;
		std::vector<Value>::iterator iterator;
	};
	//ok iterators need to hold a reference to their underlying data structure somehow
	(*this->m_ArrayPrototype)["iterator"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 1)
		{
			auto it = new arrayiter;
			it->container = v->m_Array;
			v->AddRef();
			it->iterator = v->m_Array->m_Data.begin();
			return Value(context->CreateNewUserData(it, context->m_ArrayIterPrototype));
		}
		throw RuntimeException("Bad call to getIterator");
	});

	this->m_ObjectPrototype = new JetObject(this);
	this->m_ObjectPrototype->m_Prototype = 0;
	(*this->m_ObjectPrototype)["size"] = Value([](JetContext* context, Value* v, int args)
	{
		//how do I get access to the array from here?
		if (args == 1)
			return Value((int64_t)v->m_Object->size());
		else
			throw RuntimeException("Invalid size call!!");
	});

	struct objiter
	{
		JetObject* container;
		Value current;
		JetObject::Iterator iterator;
	};
	(*this->m_ObjectPrototype)["iterator"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args == 1)
		{
			auto it = new objiter;
			it->container = v->m_Object;
			v->AddRef();
			it->iterator = v->m_Object->begin();
			return (context->CreateNewUserData(it, context->m_ObjectIterPrototype));
		}
		throw RuntimeException("Bad call to getIterator");
	});
	this->m_ObjectIterPrototype = new JetObject(this);
	this->m_ObjectIterPrototype->m_Prototype = 0;
	(*this->m_ObjectIterPrototype)["current"] = Value([](JetContext* context, Value* v, int args)
	{
		auto iterator = v->GetUserdata<objiter>();
		return iterator->current;
	});
	(*this->m_ObjectIterPrototype)["advance"] = Value([](JetContext* context, Value* v, int args)
	{
		auto iterator = v->GetUserdata<objiter>();
		if (iterator->iterator == iterator->container->end())
			return Value::Zero;

		iterator->current = iterator->iterator->second;
		++iterator->iterator;
		return Value::One;
	});
	(*this->m_ObjectIterPrototype)["_gc"] = Value([](JetContext* context, Value* v, int args)
	{
		auto iter = v->GetUserdata<objiter>();
		Value(iter->container).Release();
		delete iter;
		return Value::Empty;
	});

	this->m_ArrayIterPrototype = new JetObject(this);
	this->m_ArrayIterPrototype->m_Prototype = 0;
	(*this->m_ArrayIterPrototype)["current"] = Value([](JetContext* context, Value* v, int args)
	{
		auto iterator = v->GetUserdata<arrayiter>();
		return iterator->current;
	});

	(*this->m_ArrayIterPrototype)["advance"] = Value([](JetContext* context, Value* v, int args)
	{
		auto iterator = v->GetUserdata<arrayiter>();
		if (iterator->iterator == iterator->container->m_Data.end())
			return Value::Zero;

		iterator->current = *iterator->iterator;
		++iterator->iterator;
		return Value::One;
	});
	(*this->m_ArrayIterPrototype)["_gc"] = Value([](JetContext* context, Value* v, int args)
	{
		auto iter = v->GetUserdata<arrayiter>();
		Value(iter->container).Release();
		delete iter;
		return Value::Empty;
	});

	this->m_FunctionPrototype = new JetObject(this);
	this->m_FunctionPrototype->m_Prototype = 0;
	(*this->m_FunctionPrototype)["done"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args >= 1 && v->m_Type == ValueType::Function && v->m_Function->m_Generator)
		{
			if (v->m_Function->m_Generator->m_State == Generator::GeneratorState::Dead)
				return Value::One;
			else
				return Value::Zero;
		}
		throw RuntimeException("Cannot index a non generator or table");
	});
	/*ok get new iterator interface working
	add an iterator function to containers and function, that returns an iterator
	or a generator in the case of a yielding function

	match iterator functions for iteration on generators*/

	//for each loop needs current, next and advance
	//only work on functions
	(*this->m_FunctionPrototype)["iterator"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args >= 1 && v->m_Type == ValueType::Function && v->m_Function->m_Prototype->m_Generator)
		{
			if (v->m_Function->m_Generator)
			{
				if (v->m_Function->m_Generator->m_State == Generator::GeneratorState::Dead)
					return Value::Empty;
				else
					return *v;//hack for foreach loops
			}

			Closure* closure = new Closure;
			closure->m_RefCount = 0;
			closure->m_Grey = closure->m_Mark = false;
			closure->m_Prev = v->m_Function->m_Prev;
			closure->m_UpValueCount = v->m_Function->m_UpValueCount;
			closure->m_Generator = new Generator(context, v->m_Function, 0);
			if (closure->m_UpValueCount)
				closure->m_UpValues = new Capture*[closure->m_UpValueCount];
			closure->m_Prototype = v->m_Function->m_Prototype;
			context->m_GC.AddObject((GarbageCollector::gcval*)closure);

			if (closure->m_Generator->m_State == Generator::GeneratorState::Dead)
				return Value::Empty;
			else
				return Value(closure);
		}
		throw RuntimeException("Cannot index non generator");
	});

	//these only work on generators
	(*this->m_FunctionPrototype)["current"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args >= 1 && v->m_Type == ValueType::Function && v->m_Function->m_Generator)
		{
			//return last yielded value
			return v->m_Function->m_Generator->m_LastYielded;
		}
		throw RuntimeException("");
	});

	(*this->m_FunctionPrototype)["advance"] = Value([](JetContext* context, Value* v, int args)
	{
		if (args >= 1 && v->m_Type == ValueType::Function && v->m_Function->m_Generator)
		{
			//execute generator here
			context->Call(v);//todo add second arg if we have it
			if (v->m_Function->m_Generator->m_State == Generator::GeneratorState::Dead)
				return Value::Zero;
			else
				return Value::One;
		}
		throw RuntimeException("");
	});

	//load default libraries
	RegisterFileLibrary(this);
	RegisterMathLibrary(this);
};

JetContext::~JetContext()
{
	this->m_GC.Cleanup();

	for (auto ii: this->m_Functions)
		delete ii.second;

	for (auto ii: this->m_EntryPoints)
		delete ii;

	for (auto ii: this->m_Prototypes)
		delete ii;

	delete this->m_StringPrototype;
	delete this->m_ArrayPrototype;
	delete this->m_ObjectPrototype;
	delete this->m_ArrayIterPrototype;
	delete this->m_ObjectIterPrototype;
	delete this->m_FunctionPrototype;
}

#ifndef _WIN32
typedef signed long long INT64;
#endif
//INT64 rate;
std::vector<IntermediateInstruction> JetContext::Compile(const char* code, const char* filename)
{
#ifdef JET_TIME_EXECUTION
	INT64 start, end, rate;
	QueryPerformanceFrequency( (LARGE_INTEGER *)&rate );
	QueryPerformanceCounter( (LARGE_INTEGER *)&start );
#endif

	Lexer lexer = Lexer(code, filename);
	Parser parser = Parser(&lexer);

	BlockExpression* result = parser.parseAll();

	std::vector<IntermediateInstruction> out = m_Compiler.Compile(result, filename);

	delete result;

#ifdef JET_TIME_EXECUTION
	QueryPerformanceCounter( (LARGE_INTEGER *)&end );
	INT64 diff = end - start;
	double dt = ((double)diff)/((double)rate);

	m_OutputFunction("Took %lf seconds to compile\n\n", dt);
#endif

	return std::move(out);
}


class StackProfile
{
	char* name;
	INT64 start;
public:
	StackProfile(char* name)
	{
		this->name = name;
#ifdef JET_TIME_EXECUTION
#ifdef _WIN32
		//QueryPerformanceFrequency( (LARGE_INTEGER *)&rate );
		QueryPerformanceCounter( (LARGE_INTEGER *)&start );
#endif
#endif
	};

	~StackProfile()
	{
#ifdef JET_TIME_EXECUTION
#ifdef _WIN32
		INT64 end,rate;
		QueryPerformanceCounter( (LARGE_INTEGER *)&end );
		QueryPerformanceFrequency( (LARGE_INTEGER*)&rate);
		char o[100];
		INT64 diff = end - start;
		float dt = ((float)diff)/((float)rate);
		m_OutputFunction("%s took %f seconds\n", name, dt);
#endif
#endif
	}
};

void JetContext::RunGC()
{
	this->m_GC.Run();
}

unsigned int JetContext::Call(const Value* fun, unsigned int iptr, unsigned int args)
{
	if (fun->m_Type == ValueType::Function)
	{
		//let generators be called
		if (fun->m_Function->m_Generator)
		{
			m_CallStack.Push(std::pair<unsigned int, Closure*>(iptr, m_CurFrame));

			m_SP += m_CurFrame->m_Prototype->m_Locals;

			if ((m_SP - m_LocalStack) >= JET_STACK_SIZE)
				throw RuntimeException("Stack Overflow!");

			m_CurFrame = fun->m_Function;

			if (args == 0)
				m_Stack.Push(Value::Empty);
			else if (args > 1)
				for (unsigned int i = 1; i < args; i++)
					m_Stack.Pop();
			return fun->m_Function->m_Generator->Resume(this)-1;
		}
		if (fun->m_Function->m_Prototype->m_Generator)
		{
			//create generator and return it
			Closure* closure = new Closure;
			closure->m_Grey = closure->m_Mark = false;
			closure->m_Prev = fun->m_Function->m_Prev;
			closure->m_UpValueCount = fun->m_Function->m_UpValueCount;
			closure->m_RefCount = 0;
			closure->m_Generator = new Generator(this, fun->m_Function, args);
			if (closure->m_UpValueCount)
				closure->m_UpValues = new Capture*[closure->m_UpValueCount];
			closure->m_Prototype = fun->m_Function->m_Prototype;
			closure->m_Type = ValueType::Function;
			this->m_GC.AddObject((GarbageCollector::gcval*)closure);

			this->m_Stack.Push(Value(closure));
			return iptr;
		}

		//manipulate frame pointer
		m_CallStack.Push(std::pair<unsigned int, Closure*>(iptr, m_CurFrame));

		m_SP += m_CurFrame->m_Prototype->m_Locals;

		//clean out the new stack for the m_GC
		for (unsigned int i = 0; i < fun->m_Function->m_Prototype->m_Locals; i++)
		{
			m_SP[i] = Value::Empty;
		}

		if ((m_SP - m_LocalStack) >= JET_STACK_SIZE)
		{
			throw RuntimeException("Stack Overflow!");
		}

		m_CurFrame = fun->m_Function;

		Function* func = m_CurFrame->m_Prototype;
		//set all the locals
		if (args <= func->m_Args)
		{
			for (int i = (int)func->m_Args-1; i >= 0; i--)
			{
				if (i < (int)args)
					m_Stack.Pop(m_SP[i]);
			}
		}
		else if (func->m_VarArg)
		{
			m_SP[func->m_Locals-1] = this->CreateNewArray();
			auto arr = &m_SP[func->m_Locals-1].m_Array->m_Data;
			arr->resize(args - func->m_Args);
			for (int i = (int)args-1; i >= 0; i--)
			{
				if (i < (int)func->m_Args)
					m_Stack.Pop(m_SP[i]);
				else
					m_Stack.Pop((*arr)[i]);
			}
		}
		else
		{
			for (int i = (int)args-1; i >= 0; i--)
			{
				if (i < (int)func->m_Args)
					m_Stack.Pop(m_SP[i]);
				else
					m_Stack.QuickPop();
			}
		}

		//go to function
		return -1;
	}
	else if (fun->m_Type == ValueType::NativeFunction)
	{
		Value* tmp = &m_Stack._data[m_Stack.size()-args];

		//ok fix this to be cleaner and resolve stack printing
		//should just push a value to indicate that we are in a native function call
		m_CallStack.Push(std::pair<unsigned int, Closure*>(iptr, m_CurFrame));
		m_CallStack.Push(std::pair<unsigned int, Closure*>(JET_BAD_INSTRUCTION, 0));
		Closure* temp = m_CurFrame;
		m_SP += temp->m_Prototype->m_Locals;
		m_CurFrame = 0;
		Value ret = (*fun->m_NativeFunction)(this,tmp,args);
		m_Stack.QuickPop(args);
		m_SP -= temp->m_Prototype->m_Locals;		
		m_CurFrame = temp;
		m_CallStack.QuickPop(2);
		m_Stack.Push(ret);
		return iptr;
	}
	else if (fun->m_Type == ValueType::Object)
	{
		Value* tmp = &m_Stack._data[m_Stack.size()-args];
		Value ret;
		if(fun->TryCallMetamethod("_call", tmp, args, &ret))
		{
			m_Stack.QuickPop(args);
			m_Stack.Push(ret);
			return iptr;
		}
		m_Stack.QuickPop(args);
	}
	throw RuntimeException("Cannot call non function type " + std::string(fun->Type()) + "!!!");
}

Value JetContext::Execute(int iptr, Closure* frame)
{
#ifdef JET_TIME_EXECUTION
	INT64 start, rate, end;
	QueryPerformanceFrequency( (LARGE_INTEGER *)&rate );
	QueryPerformanceCounter( (LARGE_INTEGER *)&start );
#endif
	//frame and stack pointer reset
	unsigned int startcallstack = this->m_CallStack._size;
	unsigned int startstack = this->m_Stack._size;
	auto startlocalstack = this->m_SP;

	vmstack_push(m_CallStack,(std::pair<unsigned int, Closure*>(JET_BAD_INSTRUCTION, nullptr)));//bad value to get it to return;
	m_CurFrame = frame;

	try
	{
		while (m_CurFrame && iptr < (int)m_CurFrame->m_Prototype->m_Instructions.size() && iptr >= 0)
		{
			const Instruction& in = m_CurFrame->m_Prototype->m_Instructions[iptr];
			switch(in.m_Instruction)
			{
			case InstructionType::Add:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					VALUES_OP(a, b, +, += );
					break;
				}
			case InstructionType::Sub:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					VALUES_OP(a, b, -, -= );
					break;
				}
			case InstructionType::Mul:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					VALUES_OP(a, b, *, *= );
					break;
				}
			case InstructionType::Div:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					VALUES_OP(a, b, /, /= );
					break;
				}
			case InstructionType::Modulus:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					a %=b;
					break;
				}
			case InstructionType::BAnd:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					a &= b;
					break;
				}
			case InstructionType::BOr:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					a |= b;
					break;
				}
			case InstructionType::Xor:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					a^=b;
					break;
				}
			case InstructionType::BNot:
				{
					Value& a = vmstack_peek(m_Stack);
					a = ~a;
					break;
				}
			case InstructionType::LeftShift:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					a <<= b;
					break;
				}
			case InstructionType::RightShift:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					a >>= b;
					break;
				}
			case InstructionType::Incr:
				{
					Value& a = vmstack_peek(m_Stack);
					a .Increase();
					break;
				}
			case InstructionType::Decr:
				{
					Value& a = vmstack_peek(m_Stack);
					a.Decrease();
					break;
				}
			case InstructionType::Negate:
				{
					Value& a = vmstack_peek(m_Stack);
					a.Negate();
					break;
				}
			case InstructionType::Eq:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					//set_value_bool(a, a == b);
					VALUES_CMP(a, a, b, == );
					break;
				}
			case InstructionType::NotEq:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					//set_value_bool(a, !(a == b));
					VALUES_CMP(a, a, b, != );
					break;
				}
			case InstructionType::Lt:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					//set_value_bool(a, a.m_IntValue < b.m_IntValue);
					VALUES_CMP(a, a, b, < );
					break;
				}
			case InstructionType::Gt:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					//set_value_bool(a, a.m_IntValue > b.m_IntValue);
					VALUES_CMP(a, a, b, > );
					break;
				}
			case InstructionType::GtE:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					//set_value_bool(a, a.m_IntValue >= b.m_IntValue);
					VALUES_CMP(a, a, b, >= );
					break;
				}
			case InstructionType::LtE:
				{
					const Value& b = vmstack_peek(m_Stack);
					--m_Stack._size;
					Value& a = vmstack_peek(m_Stack);
					//set_value_bool(a, a.m_IntValue <= b.m_IntValue);
					VALUES_CMP(a, a, b, <= );
					break;
				}
			case InstructionType::LdNull:
				{
					vmstack_push(m_Stack,Value::Empty);
					break;
				}
			case InstructionType::LdInt:
				{
					vmstack_push(m_Stack, in.m_IntValue);
					break;
				}
			case InstructionType::LdReal:
			{
				vmstack_push(m_Stack, in.m_RealValue);
				break;
			}
			case InstructionType::LdStr:
				{
					vmstack_push(m_Stack, Value(in.m_StringLiteral ));
					break;
				}
			case InstructionType::Jump:
				{
					iptr = in.m_Value - 1;
					break;
				}
			case InstructionType::JumpTrue:
				{
					const auto& temp= vmstack_peek(m_Stack);
					if (temp.m_Type != ValueType::Null)
					{
						if (temp.m_IntValue != 0)
						{
							iptr = in.m_Value - 1;
						}
					}
					vmstack_pop(m_Stack);
					break;
				}
			case InstructionType::JumpTruePeek:
				{
					const auto& temp = vmstack_peek(m_Stack);
					if (temp.m_Type != ValueType::Null)
					{
						if (temp.m_IntValue != 0)
						{
							iptr = in.m_Value - 1;
						}
					}
					break;
				}
			case InstructionType::JumpFalse:
				{
					const auto&  temp = vmstack_peek(m_Stack);
					if (temp.m_Type != ValueType::Null)
					{
						if (temp.m_IntValue == 0)
						{
							iptr = in.m_Value - 1;
						}
					}
					else
					{
						iptr = in.m_Value - 1;
					}
					vmstack_pop(m_Stack);
					break;
				}
			case InstructionType::JumpFalsePeek:
				{
					const auto& temp = vmstack_peek(m_Stack);
					if (temp.m_Type != ValueType::Null)
					{
						if (temp.m_RealValue == 0)
						{
							iptr = in.m_Value - 1;
						}
					}
					else
					{
						iptr = in.m_Value - 1;
					}
					break;
				}
			case InstructionType::Load:
				{
					vmstack_push(m_Stack, (m_Variables[in.m_Value]));
					break;
				}
			case InstructionType::Store:
				{
					m_Stack.Pop(m_Variables[in.m_Value]);
					break;
				}
			case InstructionType::LLoad:
				{
					vmstack_push(m_Stack, (m_SP[in.m_Value]));
					break;
				}
			case InstructionType::LStore:
				{
					m_Stack.Pop(m_SP[in.m_Value]);
					break;
				}
			case InstructionType::CLoad:
				{
					auto frame = m_CurFrame;
					int index = in.m_Value2;
					while ( index++ < 0)
						frame = frame->m_Prev;

					vmstack_push(m_Stack, (*frame->m_UpValues[in.m_Value]->m_Ptr));
					break;
				}
			case InstructionType::CStore:
				{
					auto frame = m_CurFrame;
					int index = in.m_Value2;
					while ( index++ < 0)
						frame = frame->m_Prev;

					if (frame->m_Mark)
					{
						frame->m_Mark = false;
						m_GC.m_Greys.Push(frame);
					}

					if (frame->m_UpValues[in.m_Value]->m_Closed)
					{
						frame->m_UpValues[in.m_Value]->m_Value = m_Stack.Pop();
					}
					else
					{
						m_Stack.Pop(*frame->m_UpValues[in.m_Value]->m_Ptr);
					}
					break;
				}
			case InstructionType::LoadFunction:
				{
					//construct a new closure with the right number of upvalues
					//from the Func* object
					Closure* closure = new Closure;
					closure->m_Grey = closure->m_Mark = false;
					closure->m_Prev = m_CurFrame;
					closure->m_RefCount = 0;
					closure->m_Generator = 0;
					closure->m_UpValueCount = in.m_Function->m_UpValues;
					if (in.m_Function->m_UpValues)
					{
						closure->m_UpValues = new Capture*[in.m_Function->m_UpValues];
						//#ifdef _DEBUG
						for (unsigned int i = 0; i < in.m_Function->m_UpValues; i++)
							closure->m_UpValues[i] = 0;//this is done for the GC
						//#endif
						this->m_LastAdded = closure;
					}

					closure->m_Prototype = in.m_Function;
					closure->m_Type = ValueType::Function;
					m_GC.AddObject((GarbageCollector::gcval*)closure);
					vmstack_push(m_Stack, Value(closure));

					if (m_GC.m_AllocationCounter++%GC_INTERVAL == 0)
						this->RunGC();

					break;
				}
			case InstructionType::CInit:
				{
					//allocate and add new upvalue
					auto frame = m_LastAdded;
					//first see if we already have this closure open for this variable
					bool found = false;
					for (auto& ii: m_OpenCaptures)
					{
						if (ii.capture->m_Ptr == &m_SP[in.m_Value])
						{
							//we found it
							frame->m_UpValues[in.m_Value2] = ii.capture;
							found = true;
							//m_OutputFunction("Reused Capture %d %s in %s\n", in.value2, sptr[in.value].ToString().c_str(), curframe->prototype->name.c_str());

							if (frame->m_Mark)
							{
								frame->m_Mark = false;
								m_GC.m_Greys.Push(frame);
							}
							break;
						}
					}

					if (!found)
					{
						//allocate closure here
						auto capture = m_GC.New<Capture>();
						capture->m_Closed = false;
						capture->m_Grey = capture->m_Mark = false;
						capture->m_RefCount = 0;
						capture->m_Type = ValueType::Capture;
						capture->m_Ptr = &m_SP[in.m_Value];
#ifdef _DEBUG
						capture->m_UseCount = 1;
						capture->m_Owner = frame;
#endif
						frame->m_UpValues[in.m_Value2] = capture;

						OpenCapture c;
						c.capture = capture;
#ifdef _DEBUG
						c.creator = frame->m_Prev;
#endif
						//m_OutputFunction("Initalized Capture %d %s in %s\n", in.value2, sptr[in.value].ToString().c_str(), curframe->prototype->name.c_str());
						this->m_OpenCaptures.push_back(c);

						if (frame->m_Mark)
						{
							frame->m_Mark = false;
							m_GC.m_Greys.Push(frame);
						}

						if (m_GC.m_AllocationCounter++%GC_INTERVAL == 0)
							this->RunGC();
					}

					break;
				}
			case InstructionType::Close:
				{
					//remove from the back
					while (m_OpenCaptures.size() > 0)
					{
						auto cur = m_OpenCaptures.back();
						int index = (int)(cur.capture->m_Ptr - m_SP);
						if (index < in.m_Value)
							break;

#ifdef _DEBUG
						//this just verifies that the break above works right
						if (cur.creator != this->m_CurFrame)
							throw RuntimeException("RUNTIME ERROR: Tried to close capture in wrong scope!");
#endif

						cur.capture->m_Closed = true;
						cur.capture->m_Value = *cur.capture->m_Ptr;
						cur.capture->m_Ptr = &cur.capture->m_Value;
						//m_OutputFunction("Closed capture with value %s\n", cur->value.ToString().c_str());
						//m_OutputFunction("Closed capture %d in %d as %s\n", i, cur, cur->upvals[i]->v->ToString().c_str());

						//do a write barrier
						if (cur.capture->m_Value.m_Type > ValueType::NativeFunction && cur.capture->m_Value.m_Object->m_Grey == false)
						{
							cur.capture->m_Value.m_Object->m_Grey = true;
							this->m_GC.m_Greys.Push(cur.capture->m_Value);
						}
						m_OpenCaptures.pop_back();
					}

					break;
				}
			case InstructionType::Call:
				{
					iptr = (int)this->Call(&m_Variables[in.m_Value], iptr, in.m_Value2);
					break;
				}
			case InstructionType::ECall:
				{
					//allocate capture area here
					Value one;
					m_Stack.Pop(one);
					iptr = (int)this->Call(&one, iptr, in.m_Value);
					break;
				}
			case InstructionType::Return:
				{
					auto& oframe = vmstack_peek(m_CallStack);
					iptr = oframe.first;
					if (m_CurFrame && m_CurFrame->m_Generator)
						m_CurFrame->m_Generator->Kill();

					if (oframe.first != JET_BAD_INSTRUCTION)
					{
#ifdef _DEBUG
						//this makes sure that the m_GC doesnt overrun its boundaries
						for (int i = 0; i < (int)oframe.second->m_Prototype->m_Locals; i++)
						{
							//need to mark stack with garbage values for error checking
							m_SP[i].m_Type = ValueType::Object;
							m_SP[i].m_Object = (JetObject*)0xcdcdcdcd;
						}
#endif
						m_SP -= oframe.second->m_Prototype->m_Locals;
					}
					//m_OutputFunction("Return: Stack Ptr At: %d\n", sptr - localstack);
					m_CurFrame = oframe.second;
					vmstack_pop(m_CallStack);
					break;
				}
			case InstructionType::Yield:
				{
					if (m_CurFrame->m_Generator)
						m_CurFrame->m_Generator->Yield(this, iptr);
					else
						throw RuntimeException("Cannot Yield from outside a generator");

					auto oframe = m_CallStack.Pop();
					iptr = oframe.first;
					m_CurFrame = oframe.second;
					if (oframe.second)
						m_SP -= oframe.second->m_Prototype->m_Locals;

					break;
				}
			case InstructionType::Resume:
				{
					//resume last item placed on stack
					Value v = this->m_Stack.Pop();
					if (v.m_Type != ValueType::Function || v.m_Function->m_Generator == 0)
						throw RuntimeException("Cannot resume a non generator!");

					vmstack_push(m_CallStack,(std::pair<unsigned int, Closure*>(iptr, m_CurFrame)));

					m_SP += m_CurFrame->m_Prototype->m_Locals;

					if ((m_SP - m_LocalStack) >= JET_STACK_SIZE)
						throw RuntimeException("Stack Overflow!");

					m_CurFrame = v.m_Function;

					iptr = v.m_Function->m_Generator->Resume(this)-1;

					break;
				}
			case InstructionType::Dup:
				{
					vmstack_push_top(m_Stack);
					break;
				}
			case InstructionType::Pop:
				{
					vmstack_pop(m_Stack);
					break;
				}
			case InstructionType::StoreAt:
				{
					if (in.m_String)
					{
						Value& loc = vmstack_peek(m_Stack);
						Value& val = vmstack_peekn(m_Stack,2);

						if (loc.m_Type == ValueType::Object)
							(*loc.m_Object)[in.m_String] = val;
						else
							throw RuntimeException("Could not index a non array/object value!");
						vmstack_popn(m_Stack,2);
						//this may be redundant and already done in object
						//check me
						if (loc.m_Object->m_Mark)
						{
							//reset to grey and push back for reprocessing
							loc.m_Object->m_Mark = false;
							m_GC.m_Greys.Push(loc);//push to grey stack
						}
						//write barrier
					}
					else
					{
						Value& index = vmstack_peekn(m_Stack,1);
						Value& loc = vmstack_peekn(m_Stack,2);
						Value& val = vmstack_peekn(m_Stack,3);	

						if (loc.m_Type == ValueType::Array)
						{
							int in = (int)index;
							if (in >= (int)loc.m_Array->m_Data.size() || in < 0)
								throw RuntimeException("Array index out of range!");
							loc.m_Array->m_Data[in] = val;

							//write barrier
							if (loc.m_Array->m_Mark)
							{
								//reset to grey and push back for reprocessing
								//m_OutputFunction("write barrier triggered!\n");
								loc.m_Array->m_Mark = false;
								m_GC.m_Greys.Push(loc);//push to grey stack
							}
						}
						else if (loc.m_Type == ValueType::Object)
						{
							(*loc.m_Object)[index] = val;

							//write barrier
							//this may be redundant, lets check
							//its also done in the object object
							if (loc.m_Object->m_Mark)
							{
								//reset to grey and push back for reprocessing
								//m_OutputFunction("write barrier triggered!\n");
								loc.m_Object->m_Mark = false;
								m_GC.m_Greys.Push(loc);//push to grey stack
							}
						}
						else if (loc.m_Type == ValueType::String)
						{
							int in = (int)index;
							if (in >= (int)loc.m_Length || in < 0)
								throw RuntimeException("String index out of range!");

							loc.m_String->m_Data[in] = (int)val;
						}
						else
						{
							throw RuntimeException("Could not index a non array/object value!");
						}
						vmstack_popn(m_Stack, 3);
					}
					break;
				}
			case InstructionType::LoadAt:
				{
					if (in.m_String)
					{
						Value loc;
						m_Stack.Pop(loc);
						if (loc.m_Type == ValueType::Object)
						{
							auto n = loc.m_Object->findNode(in.m_String);
							if (n)
							{
								vmstack_push(m_Stack, n->second);
							}
							else
							{
								auto obj = loc.m_Object->m_Prototype;
								while (obj)
								{
									n = obj->findNode(in.m_String);
									if (n)
									{
										vmstack_push(m_Stack, n->second);
										break;
									}
									obj = obj->m_Prototype;
								}
							}
						}
						else if (loc.m_Type == ValueType::String)
							vmstack_push(m_Stack, ((*this->m_StringPrototype)[in.m_String]));
						else if (loc.m_Type == ValueType::Array)
							vmstack_push(m_Stack, ((*this->m_ArrayPrototype)[in.m_String]));
						else if (loc.m_Type == ValueType::Userdata)
							vmstack_push(m_Stack, ((*loc.m_UserData->m_Prototype)[in.m_String]));
						else if (loc.m_Type == ValueType::Function && loc.m_Function->m_Prototype->m_Generator)
							vmstack_push(m_Stack, ((*this->m_FunctionPrototype)[in.m_String]));
						else
							throw RuntimeException("Could not index a non array/object value!");
					}
					else
					{
						Value index = m_Stack.Pop();
						Value loc = m_Stack.Pop();

						if (loc.m_Type == ValueType::Array)
						{
							int in = (int)index;
							if (in >= (int)loc.m_Array->m_Data.size() || in < 0)
								throw RuntimeException("Array index out of range!");
							vmstack_push(m_Stack, (loc.m_Array->m_Data[in]));
						}
						else if (loc.m_Type == ValueType::Object)
							vmstack_push(m_Stack,((*loc.m_Object).get(index)));
						else if (loc.m_Type == ValueType::String)
						{
							int in = (int)index;
							if (in >= (int)loc.m_Length || in < 0)
								throw RuntimeException("String index out of range!");

							vmstack_push(m_Stack, Value(loc.m_String->m_Data[in]));
						}

						else
							throw RuntimeException("Could not index a non array/object value!");
					}
					break;
				}
			case InstructionType::NewArray:
				{
					auto arr = new JetArray();//GCVal<std::vector<Value>>();
					arr->m_Grey = arr->m_Mark = false;
					arr->m_RefCount = 0;
					arr->m_Context = this;
					arr->m_Type = ValueType::Array;
					this->m_GC.m_Generation1.push_back((GarbageCollector::gcval*)arr);
					arr->m_Data.resize(in.m_Value);
					for (int i = in.m_Value - 1; i >= 0; i--)
					{
						m_Stack.Pop(arr->m_Data[i]);
					}
					vmstack_push(m_Stack,(Value(arr)));

					if (m_GC.m_AllocationCounter++%GC_INTERVAL == 0)
						this->RunGC();

					break;
				}
			case InstructionType::NewObject:
				{
					auto obj = new JetObject(this);
					obj->m_Grey = obj->m_Mark = false;
					obj->m_RefCount = 0;
					obj->m_Type = ValueType::Object;
					this->m_GC.m_Generation1.push_back((GarbageCollector::gcval*)obj);
					for (int i = in.m_Value-1; i >= 0; i--)
					{
						const auto& value = vmstack_peek(m_Stack);
						const auto& key = vmstack_peekn(m_Stack,2);
						(*obj)[key] = value;
						vmstack_popn(m_Stack,2);
					}
					vmstack_push(m_Stack,Value(obj));

					if (m_GC.m_AllocationCounter++%GC_INTERVAL == 0)
						this->RunGC();

					break;
				}
			default:
				throw RuntimeException("Unimplemented Instruction!");
			}

			iptr++;
		}
	}
	catch(RuntimeException e)
	{
		if (e.processed == false)
		{
			m_OutputFunction("RuntimeException: %s\nCallstack:\n", e.reason.c_str());

			//generate call stack
			this->StackTrace(iptr, m_CurFrame);

			if (m_CurFrame && m_CurFrame->m_Prototype->m_Locals)
			{
				m_OutputFunction("\nLocals:\n");
				for (unsigned int i = 0; i < m_CurFrame->m_Prototype->m_Locals; i++)
				{
					Value v = this->m_SP[i];
					if (v.m_Type >= ValueType(0))
						m_OutputFunction("%s = %s\n", m_CurFrame->m_Prototype->debuglocal[i].c_str(), v.ToString().c_str());
				}
			}

			if (m_CurFrame && m_CurFrame->m_Prototype->m_UpValues)
			{
				m_OutputFunction("\nCaptures:\n");
				for (unsigned int i = 0; i < m_CurFrame->m_Prototype->m_UpValues; i++)
				{
					Value v = *m_CurFrame->m_UpValues[i]->m_Ptr;
					if (v.m_Type >= ValueType(0))
						m_OutputFunction("%s = %s\n", m_CurFrame->m_Prototype->debugcapture[i].c_str(), v.ToString().c_str());
				}
			}

			m_OutputFunction("\nGlobals:\n");
			for (auto ii: m_VariableIndex)
			{
				if (m_Variables[ii.second].m_Type != ValueType::Null)
					m_OutputFunction("%s = %s\n", ii.first.c_str(), m_Variables[ii.second].ToString().c_str());
			}
			e.processed = true;
		}

		//make sure I reset everything in the event of an error

		//clear the stacks
		this->m_CallStack.QuickPop(this->m_CallStack.size()-startcallstack);
		this->m_Stack.QuickPop(this->m_Stack.size()-startstack);

		//reset the local variable stack
		this->m_SP = startlocalstack;

		//ok add the exception details to the exception as a string or something rather than just printing them
		//maybe add more details to the exception when rethrowing
		throw e;
	}
	catch(...)
	{
		//this doesnt work right
		m_OutputFunction("Caught Some Other Exception\n\nCallstack:\n");

		this->StackTrace(iptr, m_CurFrame);

		m_OutputFunction("\\Globals:\n");
		for (auto ii: m_VariableIndex)
		{
			m_OutputFunction("%s = %s\n", ii.first.c_str(), m_Variables[ii.second].ToString().c_str());
		}

		//ok, need to properly roll back callstack
		this->m_CallStack.QuickPop(this->m_CallStack.size()-startcallstack);
		this->m_Stack.QuickPop(this->m_Stack.size()-startstack);

		//reset the local variable stack
		this->m_SP = startlocalstack;

		//rethrow the exception
		auto exception = RuntimeException("Unknown Exception Thrown From Native!");
		exception.processed = true;
		throw exception;
	}


#ifdef JET_TIME_EXECUTION
	QueryPerformanceCounter( (LARGE_INTEGER *)&end );

	INT64 diff = end - start;
	double dt = ((double)diff)/((double)rate);

	m_OutputFunction("Took %lf seconds to execute\n\n", dt);
#endif

#ifdef _DEBUG
	//debug checks for stack and what not
	if (this->m_CallStack.size() == 0)
	{
		if (this->m_SP != this->m_LocalStack)
			throw RuntimeException("FATAL ERROR: Local stack did not properly reset");
	}

	//check for stack leaks
	if (this->m_Stack.size() > startstack+1)
	{
		this->m_Stack.QuickPop(m_Stack.size());
		throw RuntimeException("FATAL ERROR: Stack leak detected!");
	}
#endif

	return m_Stack.Pop();
}

void JetContext::GetCode(int ptr, Closure* closure, std::string& ret, unsigned int& line)
{
	if (closure->m_Prototype->debuginfo.size() == 0)//make sure we have debug info
	{
		ret = "No Debug Line Info";
		line = 0;
		return;
	}

	int imax = (int)closure->m_Prototype->debuginfo.size()-1;
	int imin = 0;
	while (imax >= imin)
	{
		// calculate the midpoint for roughly equal partition
		int imid = (imin+imax)/2;//midpoint(imin, imax);
		if(closure->m_Prototype->debuginfo[imid].code == ptr)
		{
			// key found at index imid
			ret = closure->m_Prototype->debuginfo[imid].file;
			line = closure->m_Prototype->debuginfo[imid].line;
			return;// imid; 
		}
		// determine which subarray to search
		else if ((int)closure->m_Prototype->debuginfo[imid].code < ptr)
			// change min index to search upper subarray
			imin = imid + 1;
		else         
			// change max index to search lower subarray
			imax = imid - 1;
	}
#undef min
#undef max
	unsigned int index = std::max(std::min(imin, imax),0);

	ret = closure->m_Prototype->debuginfo[index].file;
	line = closure->m_Prototype->debuginfo[index].line;
}

void JetContext::StackTrace(int curiptr, Closure* cframe)
{
	auto tempcallstack = this->m_CallStack.Copy();
	if (m_CurFrame)
		tempcallstack.Push(std::pair<unsigned int, Closure*>(curiptr,cframe));

	while(tempcallstack.size() > 0)
	{
		auto top = tempcallstack.Pop();
		int greatest = -1;

		if (top.first == JET_BAD_INSTRUCTION)
			m_OutputFunction("{Native}\n");
		else
		{
			std::string fun = top.second->m_Prototype->m_Name;
			std::string file;
			unsigned int line;
			this->GetCode(top.first, top.second, file, line);
			m_OutputFunction("%s() %s Line %d (Instruction %d)\n", fun.c_str(), file.c_str(), line, top.first);
		}
	}
}

Value JetContext::Assemble(const std::vector<IntermediateInstruction>& code)
{
#ifdef JET_TIME_EXECUTION
	INT64 start, rate, end;
	QueryPerformanceFrequency( (LARGE_INTEGER *)&rate );
	QueryPerformanceCounter( (LARGE_INTEGER *)&start );
#endif
	std::map<std::string, unsigned int> labels;
	int labelposition = 0;

	for (auto inst: code)
	{
		switch (inst.type)
		{
		case InstructionType::Capture:
		case InstructionType::Local:
		case InstructionType::DebugLine:
			{
				break;
			}
		case InstructionType::Function:
			{
				labelposition = 0;

				//do something with argument and local counts
				Function* func = new Function;
				func->m_Args = inst.a;
				func->m_Locals = inst.b;
				func->m_UpValues = inst.c;
				func->m_Name = inst.string;
				func->m_Context = this;
				func->m_Generator = inst.d & 2 ? true : false;
				func->m_VarArg = inst.d & 1? true : false;

				if (m_Functions.find(inst.string) == m_Functions.end())
				{
					m_Functions[inst.string] = func;
				}
				else if (strcmp(inst.string, "{Entry Point}") == 0)
				{
					//have to do something with old entry point because it leaks
					m_EntryPoints.push_back(m_Functions[inst.string]);
					m_Functions[inst.string] = func;
				}
				else
					throw RuntimeException("ERROR: Duplicate Function Label Name: " + std::string(inst.string)+"\n");
				break;
			}
		case InstructionType::Label:
			{
				if (labels.find(inst.string) == labels.end())
				{
					labels[inst.string] = labelposition;
					delete[] inst.string;
				}
				else
				{
					delete[] inst.string;
					throw RuntimeException("ERROR: Duplicate Label Name:" + std::string(inst.string)+"\n");
				}
				break;
			}
		default:
			{
				labelposition++;
			}
		}
	}

	Function* current = 0;
	for (auto inst: code)
	{
		switch (inst.type)
		{
		case InstructionType::Local:
			{
				current->debuglocal.push_back(inst.string);
				delete[] inst.string;
				break;
			}
		case InstructionType::Capture:
			{
				current->debugcapture.push_back(inst.string);
				delete[] inst.string;
				break;
			}
		case InstructionType::Label:
			{
				break;
			}
		case InstructionType::DebugLine:
			{
				//this should contain line/file info
				Function::DebugInfo info;
				info.file = inst.string;
				info.line = (unsigned int)inst.second;
				info.code = (unsigned int)current->m_Instructions.size();
				current->debuginfo.push_back(info);
				//push something into the array at the instruction pointer
				delete[] inst.string;
				break;
			}
		case InstructionType::Function:
			{
				current = this->m_Functions[inst.string];
				delete[] inst.string;
				break;
			}
		default:
			{
				Instruction ins;
				ins.m_Instruction = inst.type;
				ins.m_String = inst.string;
				ins.m_Value = inst.first;
				if (inst.string == 0 || inst.type == InstructionType::Call)
					ins.m_Value2 = (int)inst.second;

				switch (inst.type)
				{
				case InstructionType::Call:
				case InstructionType::Store:
				case InstructionType::Load:
					{
						if (m_VariableIndex.find(inst.string) == m_VariableIndex.end())
						{
							//添加全局变量
							m_VariableIndex[inst.string] = (unsigned int)m_VariableIndex.size();
							m_Variables.push_back(Value::Empty);
						}
						ins.m_Value = m_VariableIndex[inst.string];
						delete[] inst.string;
						break;
					}
				case InstructionType::LdStr:
					{
						Value str = this->CreateNewString(inst.string, false);
						str.AddRef();
						ins.m_StringLiteral  = str.m_String;
						break;
					}
				case InstructionType::LdInt:
				{
					ins.m_IntValue = inst.int_second;
					break;
				}
				case InstructionType::LdReal:
					{
						ins.m_RealValue = inst.second;
						break;
					}
				case InstructionType::LoadFunction:
					{
						ins.m_Function = m_Functions[inst.string];
						delete[] inst.string;
						break;
					}
				case InstructionType::Jump:
				case InstructionType::JumpFalse:
				case InstructionType::JumpTrue:
				case InstructionType::JumpFalsePeek:
				case InstructionType::JumpTruePeek:
					{
						if (labels.find(inst.string) == labels.end())
							throw RuntimeException("Label '" + (std::string)inst.string + "' does not exist!");
						ins.m_Value = labels[inst.string];
						break;
					}
				case InstructionType::ForEach:
					{
						if (labels.find(inst.string) == labels.end())
							throw RuntimeException("Label '" + (std::string)inst.string + "' does not exist!");
						ins.m_Value = labels[inst.string];
						if (labels.find(inst.string2) == labels.end())
							throw RuntimeException("Label '" + (std::string)inst.string2 + "' does not exist!");
						ins.m_Value2 = labels[inst.string2];

						delete[] inst.string;
						delete[] inst.string2;
						break;
					}
				}
				current->m_Instructions.push_back(ins);
			}
		}
	}

	auto frame = new Closure;
	frame->m_Grey = frame->m_Mark = false;
	frame->m_RefCount = 0;
	frame->m_Prev = 0;
	frame->m_Generator = 0;
	frame->m_Prototype = this->m_Functions["{Entry Point}"];
	frame->m_UpValueCount = frame->m_Prototype->m_UpValues;
	frame->m_Type = ValueType::Function;
	frame->m_UpValues = 0;

	m_GC.AddObject((GarbageCollector::gcval*)frame);

#ifdef JET_TIME_EXECUTION
	QueryPerformanceCounter( (LARGE_INTEGER *)&end );

	INT64 diff = end - start;
	double dt = ((double)diff)/((double)rate);

	m_OutputFunction("Took %lf seconds to assemble\n\n", dt);
#endif

	return frame;
};


Value JetContext::Call(const Value* fun, Value* args, unsigned int numargs)
{
	if (fun->m_Type != ValueType::NativeFunction && fun->m_Type != ValueType::Function)
	{
		m_OutputFunction("ERROR: Variable is not a function\n");
		return Value::Empty;
	}
	else if (fun->m_Type == ValueType::NativeFunction)
	{
		//call it
		int s = this->m_Stack.size();
		(*fun->m_NativeFunction)(this,args,numargs);
		if (s == this->m_Stack.size())
			return Value::Empty;

		return this->m_Stack.Pop();
	}
	else if (fun->m_Function->m_Generator)
	{
		if (m_CurFrame)
			m_SP += m_CurFrame->m_Prototype->m_Locals;

		if ((m_SP - m_LocalStack) >= JET_STACK_SIZE)
			throw RuntimeException("Stack Overflow!");

		m_CurFrame = fun->m_Function;

		if (numargs == 0)
			m_Stack.Push(Value::Empty);
		else if (numargs >= 1)
			m_Stack.Push(args[0]);
		int iptr = fun->m_Function->m_Generator->Resume(this);

		return this->Execute(iptr, fun->m_Function);
	}

	if (fun->m_Function->m_Prototype->m_Generator)
	{
		//create generator and return it
		Closure* closure = new Closure;
		closure->m_Grey = closure->m_Mark = false;
		closure->m_RefCount = 0;

		closure->m_Prev = fun->m_Function->m_Prev;
		closure->m_UpValueCount = fun->m_Function->m_UpValueCount;
		closure->m_Generator = new Generator(fun->m_Function->m_Prototype->m_Context, fun->m_Function, numargs);
		if (closure->m_UpValueCount)
		{
			closure->m_UpValues = new Capture*[closure->m_UpValueCount];
			memset(closure->m_UpValues, 0, sizeof(Capture*)*closure->m_UpValueCount);
		}
		closure->m_Prototype = fun->m_Function->m_Prototype;
		closure->m_Type = ValueType::Function;
		this->m_GC.AddObject((GarbageCollector::gcval*)closure);

		return Value(closure);
	}

	bool pushed = false;
	if (this->m_CurFrame)
	{
		//need to advance stack pointer
		m_SP += m_CurFrame->m_Prototype->m_Locals;
		pushed = true;
		this->m_CallStack.Push(std::pair<unsigned int, Closure*>(0, m_CurFrame));
	}

	//clear stack values for the m_GC
	for (unsigned int i = 0; i < fun->m_Function->m_Prototype->m_Locals; i++)
		m_SP[i] = Value::Empty;

	//push args onto stack
	for (unsigned int i = 0; i < numargs; i++)
		this->m_Stack.Push(args[i]);

	auto func = fun->m_Function;
	if (numargs <= func->m_Prototype->m_Args)
	{
		for (int i = (int)func->m_Prototype->m_Args - 1; i >= 0; i--)
		{
			if (i < (int)numargs)
				m_Stack.Pop(m_SP[i]);
		}
	}
	else if (func->m_Prototype->m_VarArg)
	{
		m_SP[func->m_Prototype->m_Locals-1] = this->CreateNewArray();
		auto arr = &m_SP[func->m_Prototype->m_Locals-1].m_Array->m_Data;
		arr->resize(numargs - func->m_Prototype->m_Args);
		for (int i = (int)numargs - 1; i >= 0; i--)
		{
			if (i < (int)func->m_Prototype->m_Args)
				m_Stack.Pop(m_SP[i]);
			else
				m_Stack.Pop((*arr)[i]);
		}
	}
	else
	{
		for (int i = (int)numargs-1; i >= 0; i--)
		{
			if (i < (int)func->m_Prototype->m_Args)
				m_Stack.Pop(m_SP[i]);
			else
				m_Stack.QuickPop();
		}
	}

	Value ret = this->Execute(0, fun->m_Function);

	if (pushed)
	{
		m_CurFrame = this->m_CallStack.Pop().second;//restore
		m_SP -= m_CurFrame->m_Prototype->m_Locals;
	}
	return ret;
}

//executes a function in the VM context
Value JetContext::Call(const char* m_FunctionPrototype, Value* args, unsigned int numargs)
{
	if (m_VariableIndex.find(m_FunctionPrototype) == m_VariableIndex.end())
	{
		m_OutputFunction("ERROR: No variable named: '%s' to call\n", m_FunctionPrototype);
		return Value::Empty;
	}

	Value fun = m_Variables[m_VariableIndex[m_FunctionPrototype]];
	if (fun.m_Type != ValueType::NativeFunction && fun.m_Type != ValueType::Function)
	{
		m_OutputFunction("ERROR: Variable '%s' is not a function\n", m_FunctionPrototype);
		return Value::Empty;
	}

	return this->Call(&fun, args, numargs);
};

std::string JetContext::Script(const std::string& code, const std::string& filename)
{
	try
	{
		//try and execute
		return this->Script(code.c_str(), filename.c_str()).ToString();
	}
	catch(CompilerException E)
	{
		m_OutputFunction("Exception found:\n");
		m_OutputFunction("%s (%d): %s\n", E.file.c_str(), E.line, E.ShowReason());
		return "";
	}
}

Value JetContext::Script(const char* code, const char* filename)//compiles, assembles and executes the script
{
	auto asmb = this->Compile(code, filename);
	Value fun = this->Assemble(asmb);

	return this->Call(&fun);
}

void Jet::JetContext::SetOutputFunction(OutputFunction val)
{
	m_OutputFunction = val;
	if (m_OutputFunction == nullptr)
	{
		m_OutputFunction = printf;
	}
}

#ifdef EMSCRIPTEN
#include <emscripten/bind.h>
using namespace emscripten;
using namespace std;
EMSCRIPTEN_BINDINGS(Jet) {
	class_<Jet::JetContext>("JetContext")
		.constructor()
		.m_FunctionPrototype<std::string, std::string, std::string>("Script", &Jet::JetContext::Script);
}
#endif