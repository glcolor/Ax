#include "Value.h"
#include "JetContext.h"

using namespace Jet;
#undef Yield

Generator::Generator(JetContext* context, Closure* closure, unsigned int args)
{
	m_State = GeneratorState::Suspended;
	this->m_Closure = closure;

	//need to push args onto the stack
	this->m_Stack = new Value[closure->m_Prototype->m_Locals];

	//pass in arguments
	if (args <= closure->m_Prototype->m_Args)
	{
		for (int i = closure->m_Prototype->m_Args-1; i >= 0; i--)
		{
			if (i < (int)args)
				context->m_Stack.Pop(m_Stack[i]);
			else
				m_Stack[i] = Value::Empty;
		}
	}
	else if (closure->m_Prototype->m_VarArg)
	{
		m_Stack[closure->m_Prototype->m_Locals-1] = context->CreateNewArray();
		auto arr = &m_Stack[closure->m_Prototype->m_Locals-1].m_Array->m_Data;
		arr->resize(args - closure->m_Prototype->m_Args);
		for (int i = (int)args - 1; i >= 0; i--)
		{
			if (i < (int)closure->m_Prototype->m_Args)
				context->m_Stack.Pop(m_Stack[i]);
			else
				context->m_Stack.Pop((*arr)[i]);
		}
	}
	else
	{
		for (int i = (int)args - 1; i >= 0; i--)
		{
			if (i < (int)closure->m_Prototype->m_Args)
				context->m_Stack.Pop(m_Stack[i]);
			else
				context->m_Stack.QuickPop();
		}
	}

	this->m_CurrentIPtr = 0;//set current position to start of function
}

void Generator::Yield(JetContext* m_Context, unsigned int iptr)
{
	this->m_State = GeneratorState::Suspended;
	//store the iptr
	this->m_CurrentIPtr = iptr+1;

	this->m_LastYielded = m_Context->m_Stack.Peek();

	//store stack
	for (unsigned int i = 0; i < this->m_Closure->m_Prototype->m_Locals; i++)
		this->m_Stack[i] = m_Context->m_SP[i];
}

unsigned int Generator::Resume(JetContext* context)
{
	if (this->m_State == GeneratorState::Dead)
		throw RuntimeException("Cannot resume dead generator");
	if (this->m_State == GeneratorState::Running)
		throw RuntimeException("Cannot resume active generator");

	this->m_State = GeneratorState::Running;

	//restore stack
	for (unsigned int i = 0; i < this->m_Closure->m_Prototype->m_Locals; i++)
		context->m_SP[i] = this->m_Stack[i];

	if (this->m_CurrentIPtr == 0)
		context->m_Stack.Pop();

	return this->m_CurrentIPtr;
}

//---------------------------------------------------------------------
Jet::Value Jet::Value::Empty;
Jet::Value Jet::Value::Zero(0);
Jet::Value Jet::Value::One(1);

Value::Value()
{
	this->m_Type = ValueType::Null;
	this->m_IntValue = 0;
}

Value::Value(JetString* str)
{
	if (str == 0)
		return;

	m_Type = ValueType::String;
	m_Length = (unsigned int)strlen(str->m_Data);
	m_String = str;
}

Value::Value(JetObject* obj)
{
	this->m_Type = ValueType::Object;
	this->m_Object = obj;
}

Value::Value(JetArray* arr)
{
	m_Type = ValueType::Array;
	this->m_Array = arr;
}

Value::Value(double val)
{
	m_Type = ValueType::Real;
	m_RealValue = val;
}

Value::Value(int val)
{
	m_Type = ValueType::Int;
	m_IntValue = val;
}

Value::Value(int64_t val)
{
	m_Type = ValueType::Int;
	m_IntValue = val;
}

Value::Value(JetNativeFunc a)
{
	m_Type = ValueType::NativeFunction;
	m_NativeFunction = a;
}

Value::Value(Closure* func)
{
	m_Type = ValueType::Function;
	m_Function = func;
}

Value::Value(JetUserdata* userdata, JetObject* prototype)
{
	this->m_Type = ValueType::Userdata;
	this->m_UserData = userdata;
	this->m_UserData->m_Prototype = prototype;
}

JetObject* Value::GetPrototype()
{
	//add defaults for string and array
	switch (m_Type)
	{
	case ValueType::Array:
		return 0;
	case ValueType::Object:
		return this->m_Object->m_Prototype;
	case ValueType::String:
		return 0;
	case ValueType::Userdata:
		return this->m_UserData->m_Prototype;
	default:
		return 0;
	}
}

void Value::AddRef()
{
	switch (m_Type)
	{
	case ValueType::Array:
	case ValueType::Object:
		if (this->m_Object->m_RefCount == 0)
			this->m_Object->m_Context->m_GC.m_NativeRefs.push_back(*this);

	case ValueType::String:
	case ValueType::Userdata:
		if (this->m_Object->m_RefCount == 255)
			throw RuntimeException("Tried to addref when count was at the maximum of 255!");

		this->m_Object->m_RefCount++;
		break;
	case ValueType::Function:
		if (this->m_Function->m_RefCount == 0)
			this->m_Function->m_Prototype->m_Context->m_GC.m_NativeRefs.push_back(*this);

		if (this->m_Function->m_RefCount == 255)
			throw RuntimeException("Tried to addref when count was at the maximum of 255!");

		this->m_Function->m_RefCount++;
	}
}

void Value::Release()
{
	switch (m_Type)
	{
	case ValueType::String:
	case ValueType::Userdata:
		if (this->m_Object->m_RefCount == 0)
			throw RuntimeException("Tried to subtract from reference count of 0!");
		this->m_Object->m_RefCount--;

		break;
	case ValueType::Array:
	case ValueType::Object:
		if (this->m_Object->m_RefCount == 0)
			throw RuntimeException("Tried to subtract from reference count of 0!");
		this->m_Object->m_RefCount--;

		if (this->m_Object->m_RefCount == 0)
		{
			//remove me from the list
			//swap this and the last then remove the last
			JetContext* context = this->m_Object->m_Context;
			if (context->m_GC.m_NativeRefs.size() > 1)
			{
				for (unsigned int i = 0; i < context->m_GC.m_NativeRefs.size(); i++)
				{
					if (context->m_GC.m_NativeRefs[i] == *this)
					{
						context->m_GC.m_NativeRefs[i] = context->m_GC.m_NativeRefs[context->m_GC.m_NativeRefs.size()-1];
						break;
					}
				}
			}
			this->m_Object->m_Context->m_GC.m_NativeRefs.pop_back();
		}
		break;
	case ValueType::Function:
		if (this->m_Function->m_RefCount == 0)
			throw RuntimeException("Tried to subtract from reference count of 0!");

		this->m_Function->m_RefCount--;

		if (this->m_Function->m_RefCount == 0)
		{
			//remove me from the list
			//swap this and the last then remove the last
			JetContext* context = this->m_Function->m_Prototype->m_Context;
			if (context->m_GC.m_NativeRefs.size() > 1)
			{
				for (unsigned int i = 0; i < context->m_GC.m_NativeRefs.size(); i++)
				{
					if (context->m_GC.m_NativeRefs[i] == *this)
					{
						context->m_GC.m_NativeRefs[i] = context->m_GC.m_NativeRefs[context->m_GC.m_NativeRefs.size()-1];
						break;
					}
				}
			}
			context->m_GC.m_NativeRefs.pop_back();
		}
	}
}

std::string Value::ToString(int depth) const
{
	switch(this->m_Type)
	{
	case ValueType::Null:
		return "Null";
	case ValueType::Int:
		return std::to_string(this->m_IntValue);
	case ValueType::Real:
		return std::to_string(this->m_RealValue);
	case ValueType::String:
		return this->m_String->m_Data;
	case ValueType::Function:
		return "[Function "+this->m_Function->m_Prototype->m_Name+" " + std::to_string((unsigned int)this->m_Function)+"]";
	case ValueType::NativeFunction:
		return "[NativeFunction "+std::to_string((unsigned int)this->m_Function)+"]";
	case ValueType::Array:
		{
			std::string str = "[\n";

			if (depth++ > 3)
				return "[Array " + std::to_string((int)this->m_Array)+"]";

			int i = 0;
			for (auto ii: this->m_Array->m_Data)
			{
				str += "\t";
				str += std::to_string(i++);
				str += " = ";
				str += ii.ToString(depth) + "\n";
			}
			str += "]";
			return str;
		}
	case ValueType::Object:
		{
			std::string str = "{\n";

			if (depth++ > 3)
				return "[Object " + std::to_string((int)this->m_Object)+"]";

			for (auto ii: *this->m_Object)
			{
				str += "\t";
				str += ii.first.ToString(depth);
				str += " = ";
				str += ii.second.ToString(depth) + "\n";
			}
			str += "}";
			return str;
		}
	case ValueType::Userdata:
		{
			return "[Userdata "+std::to_string((int)this->m_UserData)+"]";
		}
	default:
		return "";
	}
}

void Value::SetPrototype(JetObject* obj)
{
	switch (this->m_Type)
	{
	case ValueType::Object:
		this->m_Object->m_Prototype = obj;
	case ValueType::Userdata:
		this->m_UserData->m_Prototype = obj;
	default:
		throw RuntimeException("Cannot set prototype of non-object or non-userdata!");
	}
}

Value Value::CallMetamethod(JetObject* table, const char* name, const Value* other)
{
	auto node = table->m_Prototype->findNode(name);
	if (node == 0)
	{
		auto obj = table->m_Prototype;
		while(obj)
		{
			node = obj->findNode(name);
			if (node)
				break;
			obj = obj->m_Prototype;
		}
	}

	if (node)
	{
		Value args[2];
		args[0] = *this;
		if (other)
			args[1] = *other;
		return table->m_Prototype->m_Context->Call(&node->second, (Value*)&args, other ? 2 : 1);
	}

	throw RuntimeException("Cannot " + (std::string)(name+1) + " two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other->m_Type]);
}

Value Value::CallMetamethod(const char* name, const Value* other)
{
	auto node = this->m_Object->m_Prototype->findNode(name);
	if (node == 0)
	{
		auto obj = this->m_Object->m_Prototype;
		while(obj)
		{
			node = obj->findNode(name);
			if (node)
				break;
			obj = obj->m_Prototype;
		}
	}

	if (node)
	{
		Value args[2];
		args[0] = *this;
		if (other)
			args[1] = *other;
		return this->m_Object->m_Prototype->m_Context->Call(&node->second, (Value*)&args, other ? 2 : 1);
	}

	throw RuntimeException("Cannot " + (std::string)(name+1) + " two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other->m_Type]);
}

bool Value::TryCallMetamethod(const char* name, const Value* iargs, int numargs, Value* out) const
{
	auto node = this->m_Object->m_Prototype->findNode(name);
	if (node == 0)
	{
		auto obj = this->m_Object->m_Prototype;
		while(obj)
		{
			node = obj->findNode(name);
			if (node)
				break;
			obj = obj->m_Prototype;
		}
	}

	if (node)
	{
		//fix this not working with arguments > 1
		Value* args = (Value*)alloca(sizeof(Value)*(numargs+1));
		//Value args[2];
		args[numargs] = *this;
		for (int i = 0; i < numargs; i++)
			args[i] = iargs[i];

		//help, calling this derps up curframe
		*out = this->m_Object->m_Prototype->m_Context->Call(&node->second, args, numargs+1);
		return true;
	}
	return false;
}

Value Value::operator()(JetContext* context, Value* v, int args)
{
	return context->Call(this, v, args);
}

Value Value::Call(Value* v, int args)
{
	if (this->m_Type == ValueType::Function)
	{
		return this->m_Function->m_Prototype->m_Context->Call(this, v, args);
	}
	else if (this->m_Type == ValueType::NativeFunction)
	{
		throw RuntimeException("Not implemented!");
	}

	throw RuntimeException("Cannot call non function!");
}

bool Value::operator== (const Value& other) const
{
	if (other.m_Type != this->m_Type)
		return false;

	switch (this->m_Type)
	{
	case ValueType::Int:
		return other.m_IntValue == this->m_IntValue;
	case ValueType::Real:
		return other.m_RealValue == this->m_RealValue;
	case ValueType::Array:
		return other.m_Array == this->m_Array;
	case ValueType::Function:
		return other.m_Function == this->m_Function;
	case ValueType::NativeFunction:
		return other.m_NativeFunction == this->m_NativeFunction;
	case ValueType::String:
		return strcmp(other.m_String->m_Data, this->m_String->m_Data) == 0;
	case ValueType::Null:
		return true;
	case ValueType::Object:
		return other.m_Object == this->m_Object;
	case ValueType::Userdata:
		return other.m_UserData == this->m_UserData;
	}
	return false;
}

Value& Value::operator[] (int64_t key)
{
	switch (m_Type)
	{
	case ValueType::Array:
		return this->m_Array->m_Data[(size_t)key];
	case ValueType::Object:
		return (*this->m_Object)[key];
	default:
		throw RuntimeException("Cannot index type " + (std::string)ValueTypes[(int)this->m_Type]);
	}
}

Value& Value::operator[] (const char* key)
{
	switch (m_Type)
	{
	case ValueType::Object:
		{
			return (*this->m_Object)[key];
		}
	default:
		throw RuntimeException("Cannot index type " + (std::string)ValueTypes[(int)this->m_Type]);
	}
}

Value& Value::operator[] (const Value& key)
{
	switch (m_Type)
	{
	case ValueType::Array:
		{
			return this->m_Array->m_Data[(int)key.m_RealValue];
		}
	case ValueType::Object:
		{
			return (*this->m_Object)[key];
		}
	default:
		throw RuntimeException("Cannot index type " + (std::string)ValueTypes[(int)this->m_Type]);
	}
}

Value Value::operator+( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value((double)m_IntValue + other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue + other.m_IntValue);
		else if (other.m_Type == ValueType::String)
		{
			if (other.m_String->m_Context == nullptr) return *this;
			std::string str = this->ToString() + std::string(other.m_String->m_Data);
			return other.m_String->m_Context->CreateNewString(str.c_str(), true);
		}
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value(m_RealValue + other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_RealValue + (double)other.m_IntValue);
		else if (other.m_Type == ValueType::String)
		{
			if (other.m_String->m_Context == nullptr) return *this;
			std::string str = this->ToString() + std::string(other.m_String->m_Data);
			return other.m_String->m_Context->CreateNewString(str.c_str(), true);
		}
		break;
	case ValueType::String:
	{
		if (this->m_String->m_Context == nullptr) return *this;
		std::string str = std::string(this->m_String->m_Data) + other.ToString();
		return this->m_String->m_Context->CreateNewString(str.c_str(), true);
		break;
	}
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_add", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_add", &other);
		//throw JetRuntimeException("Cannot Add A String");
		//if (other.type == ValueType::String)
		//return Value((std::string(other.string.data) + std::string(this->string.data)).c_str());
		//else
		//return Value((other.ToString() + std::string(this->string.data)).c_str());
	}

	throw RuntimeException("Cannot add two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator-( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value((double)m_IntValue - other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue - other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value(m_RealValue - other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_RealValue - (double)other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_sub", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_sub", &other);
		break;
	}

	throw RuntimeException("Cannot subtract two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator*( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value((double)m_IntValue * other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue * other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value(m_RealValue * other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_RealValue * (double)other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_mul", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_mul", &other);
		break;
	}

	throw RuntimeException("Cannot multiply two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator/( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value((double)m_IntValue / other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value((double)m_IntValue / (double)other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value(m_RealValue / other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_RealValue / (double)other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_div", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_div", &other);
		break;
	}

	throw RuntimeException("Cannot divide two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator%( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
		{
			return Value(m_IntValue % (int64_t)other.m_RealValue);
		}
		else if (other.m_Type == ValueType::Int)
		{
			return Value(m_IntValue % other.m_IntValue);
		}
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
		{
			return Value((int64_t)m_RealValue % (int64_t)other.m_RealValue);
		}
		else if (other.m_Type == ValueType::Int)
		{
			return Value((int64_t)m_RealValue % other.m_IntValue);
		}
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_mod", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_mod", &other);
		break;
	}

	throw RuntimeException("Cannot modulus two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator|( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value(m_IntValue |(int64_t) other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue | other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value((int64_t)m_RealValue | (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value((int64_t)m_RealValue | other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_bor", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_bor", &other);
		break;
	}

	throw RuntimeException("Cannot binary or two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator&( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value(m_IntValue & (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue & other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value((int64_t)m_RealValue & (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value((int64_t)m_RealValue & other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_band", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_band", &other);
		break;
	}

	throw RuntimeException("Cannot binary and two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator^( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value(m_IntValue ^ (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue ^ other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value((int64_t)m_RealValue ^ (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value((int64_t)m_RealValue ^ other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_xor", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_xor", &other);
		break;
	}

	throw RuntimeException("Cannot xor two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator<<( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value(m_IntValue << (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue << other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value((int64_t)m_RealValue << (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value((int64_t)m_RealValue << other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_ls", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_ls", &other);
		break;
	}

	throw RuntimeException("Cannot left-shift two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator>>( const Value &other )
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		if (other.m_Type == ValueType::Real)
			return Value(m_IntValue >> (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value(m_IntValue >> other.m_IntValue);
		break;
	case ValueType::Real:
		if (other.m_Type == ValueType::Real)
			return Value((int64_t)m_RealValue >> (int64_t)other.m_RealValue);
		else if (other.m_Type == ValueType::Int)
			return Value((int64_t)m_RealValue >> other.m_IntValue);
		break;
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_rs", &other);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_rs", &other);
		break;
	}

	throw RuntimeException("Cannot right-shift two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};


void Value::operator+=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
		{
			switch (other.m_Type)
			{
				case ValueType::Real:
				{
					m_Type = ValueType::Real;
					m_RealValue = (double)m_IntValue + other.m_RealValue;
					return;
				}
				case ValueType::Int:
				{
					m_IntValue += other.m_IntValue;
					return;
				}
				case ValueType::String:
				{
					if (other.m_String->m_Context == nullptr) return;
					std::string str = this->ToString() + std::string(other.m_String->m_Data);
					m_Type = ValueType::String;
					*this = other.m_String->m_Context->CreateNewString(str.c_str(), true);
					return;
				}
			}
			break;
		}
		case ValueType::Real:
		{
			switch (other.m_Type)
			{
				case ValueType::Real:
				{
					m_RealValue += other.m_RealValue;
					return;
				}
				case ValueType::Int:
				{
					m_RealValue += (double)other.m_IntValue;
					return;
				}
				case ValueType::String:
				{
					if (other.m_String->m_Context == nullptr) return;
					std::string str = this->ToString() + std::string(other.m_String->m_Data);
					*this = other.m_String->m_Context->CreateNewString(str.c_str(), true);
					return;
				}
			}
			break;
		}
		case ValueType::String:
		{
			if (this->m_String->m_Context == nullptr) return;
			std::string str = std::string(this->m_String->m_Data) + other.ToString();
			*this = this->m_String->m_Context->CreateNewString(str.c_str(), true);
			return;
			break;
		}
		case ValueType::Userdata:
		{
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_add", &other);
				return;
			}
			break;
		}
		case ValueType::Object:
		{
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_add", &other);
				return;
			}
		}
	}

	throw RuntimeException("Cannot add two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator-=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
		{
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Real;
				m_RealValue = (double)m_IntValue - other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue -= other.m_IntValue;
				return;
			}
			break;
		}
		case ValueType::Real:
		{
			if (other.m_Type == ValueType::Real)
			{
				m_RealValue -= other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_RealValue -= (double)other.m_RealValue;
				return;
			}
			break;
		}
		case ValueType::Userdata:
		{
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_sub", &other);
				return;
			}
			break;
		}
		case ValueType::Object:
		{
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_sub", &other);
				return;
			}
			break;
		}
	}

	throw RuntimeException("Cannot subtract two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator*=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Real;
				m_RealValue = (double)m_IntValue * other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue *= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_RealValue *= other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_RealValue *= (double)other.m_RealValue;
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_mul", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_mul", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot multiply two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator/=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Real;
				m_RealValue = (double)m_IntValue / other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue /= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_RealValue /= other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_RealValue /= (double)other.m_RealValue;
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_div", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_div", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot divide two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator%=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Real;
				m_RealValue = fmod((double)m_IntValue, other.m_RealValue);
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue %= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_RealValue = fmod(m_RealValue, other.m_RealValue);
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_RealValue = fmod(m_RealValue, (double)other.m_IntValue);
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_mod", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_mod", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot modulus two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator|=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_IntValue |= (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue |= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue | (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue | other.m_IntValue;
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_or", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_or", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot binary or two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator&=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_IntValue &= (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue &= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue & (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue & other.m_IntValue;
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_and", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_and", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot binary and two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator^=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_IntValue ^= (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue ^= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue ^ (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue ^ other.m_IntValue;
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_xor", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_xor", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot xor two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator<<=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_IntValue <<= (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue <<= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue << (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue << other.m_IntValue;
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_ls", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_ls", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot left-shift two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

void Value::operator>>=(const Value &other)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			if (other.m_Type == ValueType::Real)
			{
				m_IntValue >>= (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_IntValue >>= other.m_IntValue;
				return;
			}
			break;
		case ValueType::Real:
			if (other.m_Type == ValueType::Real)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue >> (int64_t)other.m_RealValue;
				return;
			}
			else if (other.m_Type == ValueType::Int)
			{
				m_Type = ValueType::Int;
				m_IntValue = (int64_t)m_RealValue >> other.m_IntValue;
				return;
			}
			break;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_rs", &other);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_rs", &other);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot right-shift two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)other.m_Type]);
};

Value Value::operator~()
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		return Value(~m_IntValue);
	case ValueType::Real:
		return Value(~(int64_t)m_RealValue);
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_bnot", 0);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_bnot", 0);
		break;
	}

	throw RuntimeException("Cannot binary complement non-numeric type! " + (std::string)ValueTypes[(int)this->m_Type]);
};

Value Value::operator-()
{
	switch(this->m_Type)
	{
	case ValueType::Int:
		return Value(-m_IntValue);
	case ValueType::Real:
		return Value(-m_RealValue);
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
			return this->CallMetamethod(this->m_UserData->m_Prototype, "_neg", 0);
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
			return this->CallMetamethod("_neg", 0);
		break;
	}

	throw RuntimeException("Cannot negate non-numeric type! " + (std::string)ValueTypes[(int)this->m_Type]);
}

int64_t Jet::Value::Compare(const Value& o)
{
	switch (this->m_Type)
	{
		case ValueType::Int:
		{
			if (o.m_Type == ValueType::Real)
				return (int64_t)((double)m_IntValue - o.m_RealValue);
			else if (o.m_Type == ValueType::Int)
				return (m_IntValue - o.m_IntValue);
			break;
		}
		case ValueType::Real:
		{
			if (o.m_Type == ValueType::Real)
				return int64_t(m_RealValue - o.m_RealValue);
			else if (o.m_Type == ValueType::Int)
				return (int64_t)(m_RealValue - (double)o.m_IntValue);
			break;
		}
		case ValueType::String:
		{
			if (o.m_Type == ValueType::String)
			{
				if (o.m_String->m_Data == m_String->m_Data) return 0;
				return strcmp(o.m_String->m_Data, this->m_String->m_Data);
			}
		}
		case ValueType::Null:
		{
			if (o.m_Type == ValueType::Null) return 0;
			return -1;
		}
		case ValueType::Userdata:
		{
			if (this->m_UserData->m_Prototype)
			{
				if (o.m_Type == ValueType::Userdata && this->m_UserData == o.m_UserData) return 0;
				return this->CallMetamethod(this->m_UserData->m_Prototype, "_cmp", &o).m_IntValue;
			}				
			break;
		}
		case ValueType::Object:
		{	
			if (this->m_Object->m_Prototype)
			{
				if (o.m_Type == ValueType::Object &&this->m_Object == o.m_Object) return 0;
				return this->CallMetamethod("_cmp", &o).m_IntValue;
			}
			break;
		}
		default:
		{
			if (o.m_Type == ValueType::Null) return 1;
			if (o.m_Type == m_Type) return ((int64_t)o.m_Array - (int64_t)this->m_Array);
			break;
		}
	}
	if (o.m_Type == ValueType::Null) return 1;
	throw RuntimeException("Cannot subtract two non-numeric types! " + (std::string)ValueTypes[(int)this->m_Type] + " and " + (std::string)ValueTypes[(int)o.m_Type]);
}

void Jet::Value::Negate()
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			m_IntValue = -m_IntValue;
			return;
		case ValueType::Real:
			m_RealValue = -m_RealValue;
			return;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_neg", 0);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_neg", 0);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot negate non-numeric type! " + (std::string)ValueTypes[(int)this->m_Type]);
}

void Jet::Value::Increase()
{
	switch (this->m_Type)
	{
		case ValueType::Int:
			++m_IntValue;
			return;
		case ValueType::Real:
			++m_RealValue;
			return;
		case ValueType::Userdata:
			if (this->m_UserData->m_Prototype)
			{
				this->CallMetamethod(this->m_UserData->m_Prototype, "_incr", 0);
				return;
			}
			break;
		case ValueType::Object:
			if (this->m_Object->m_Prototype)
			{
				this->CallMetamethod("_incr", 0);
				return;
			}
			break;
	}

	throw RuntimeException("Cannot incr non-numeric type! " + (std::string)ValueTypes[(int)this->m_Type]);
}

void Jet::Value::Decrease()
{
	switch (this->m_Type)
	{
	case ValueType::Int:
	{
		--m_IntValue;
		return;
	}
	case ValueType::Real:
	{
		--m_RealValue;
		return;
	}
	case ValueType::Userdata:
		if (this->m_UserData->m_Prototype)
		{
			this->CallMetamethod(this->m_UserData->m_Prototype, "_decr", 0);
			return;
		}
		break;
	case ValueType::Object:
		if (this->m_Object->m_Prototype)
		{
			this->CallMetamethod("_decr", 0);
			return;
		}
		break;
	}

	throw RuntimeException("Cannot decr non-numeric type! " + (std::string)ValueTypes[(int)this->m_Type]);
}
