#include "GarbageCollector.h"
#include "JetContext.h"

using namespace Jet;

//#define JETGCDEBUG

GarbageCollector::GarbageCollector(JetContext* context) : m_Context(context)
{
	this->m_AllocationCounter = 1;//messes up if these start at 0
	this->m_CollectionCounter = 1;
}

void GarbageCollector::Cleanup()
{
	//need to do a two pass system to properly destroy userdata
	//first pass of destructing
	for (auto& ii: this->m_Generation1)
	{
		switch (ii->type)
		{
		case ValueType::Function:
		case ValueType::Object:
		case ValueType::Array:
		case ValueType::String:
			break;
		case ValueType::Userdata:
			{
				Value ud = Value(((JetUserdata*)ii), ((JetUserdata*)ii)->m_Prototype);
				Value _gc = (*((JetUserdata*)ii)->m_Prototype).get("_gc");
				if (_gc.m_Type == ValueType::NativeFunction)
					_gc.m_NativeFunction(this->m_Context, &ud, 1);
				else if (_gc.m_Type == ValueType::Function)
					throw RuntimeException("Non Native _gc Hooks Not Implemented!");//todo
				else if (_gc.m_Type != ValueType::Null)
					throw RuntimeException("Invalid _gc Hook!");
				break;
			}
		}
	}

	for (auto& ii: this->m_Generation2)
	{
		switch (ii->type)
		{
		case ValueType::Function:
		case ValueType::Object:
		case ValueType::Array:
		case ValueType::String:
			break;
		case ValueType::Userdata:
			{
				Value ud = Value(((JetUserdata*)ii), ((JetUserdata*)ii)->m_Prototype);
				Value _gc = (*((JetUserdata*)ii)->m_Prototype).get("_gc");
				if (_gc.m_Type == ValueType::NativeFunction)
					_gc.m_NativeFunction(this->m_Context, &ud, 1);
				else if (_gc.m_Type == ValueType::Function)
					throw RuntimeException("Non Native _gc Hooks Not Implemented!");//todo
				else if (_gc.m_Type != ValueType::Null)
					throw RuntimeException("Invalid _gc Hook!");
				break;
			}
		}
	}

	//delete everything else
	for (auto& ii: this->m_Generation1)
	{
		switch (ii->type)
		{
		case ValueType::Function:
			{
				Closure* fun = (Closure*)ii;
				if (fun->m_UpValueCount)
					delete[] fun->m_UpValues;
				delete fun->m_Generator;
				delete fun;
				break;
			}
		case ValueType::Object:
			delete (JetObject*)ii;
			break;
		case ValueType::Array:
			((JetArray*)ii)->m_Data.~vector();
			delete[] (char*)ii;
			break;
		case ValueType::Userdata:
			//did in first pass
			delete (JetUserdata*)ii;
			break;
		case ValueType::String:
			{
				JetString* str = (JetString*)ii;
				delete[] str->m_Data;
				delete str;
				break;
			}
		case ValueType::Capture:
			{
				Capture* c = (Capture*)ii;
				delete c;
				break;
			}
		}
	}

	for (auto& ii: this->m_Generation2)
	{
		switch (ii->type)
		{
		case ValueType::Function:
			{
				Closure* fun = (Closure*)ii;
				if (fun->m_UpValueCount)
					delete[] fun->m_UpValues;
				delete fun->m_Generator;
				delete fun;
				break;
			}
		case ValueType::Object:
			delete (JetObject*)ii;
			break;
		case ValueType::Array:
			delete ((JetArray*)ii);
			break;
		case ValueType::Userdata:
			//did in first pass
			delete (JetUserdata*)ii;
			break;
		case ValueType::String:
			{
				JetString* str = (JetString*)ii;
				delete[] str->m_Data;
				delete str;
				break;
			}
		case ValueType::Capture:
			{
				Capture* c = (Capture*)ii;
				delete c;
				break;
			}
		}
	}
}

void GarbageCollector::Mark()
{
	//mark basic types
	this->m_Greys.Push(m_Context->m_ArrayPrototype);
	this->m_Greys.Push(m_Context->m_ArrayIterPrototype);
	this->m_Greys.Push(m_Context->m_ObjectPrototype);
	this->m_Greys.Push(m_Context->m_ObjectIterPrototype);
	this->m_Greys.Push(m_Context->m_StringPrototype);
	this->m_Greys.Push(m_Context->m_FunctionPrototype);

	for (unsigned int i = 0; i < this->m_Context->m_Prototypes.size(); i++)
		this->m_Greys.Push(this->m_Context->m_Prototypes[i]);

	//mark all objects being held by native code
	for (unsigned int i = 0; i < this->m_NativeRefs.size(); i++)
	{
		if (this->m_NativeRefs[i].m_Object->m_Grey == false)
		{
			this->m_NativeRefs[i].m_Object->m_Grey = true;
			this->m_Greys.Push(this->m_NativeRefs[i]);
		}
	}

	//add more write barriers to detect when objects are removed and what not
	//if flag is marked, then black
	//if no flag and grey bit, then grey
	//if no flag or grey bit, then white

	//push all reachable items onto grey stack
	//this means globals
	{
		//StackProfile profile("Mark Globals as Grey");
		for (unsigned int i = 0; i < m_Context->m_Variables.size(); i++)
		{
			if (m_Context->m_Variables[i].m_Type > ValueType::NativeFunction)
			{
				if (m_Context->m_Variables[i].m_Object->m_Grey == false)
				{
					m_Context->m_Variables[i].m_Object->m_Grey = true;
					this->m_Greys.Push(m_Context->m_Variables[i]);
				}
			}
		}
	}


	if (m_Context->m_Stack.size() > 0)
	{
		for (unsigned int i = 0; i < m_Context->m_Stack.size(); i++)
		{
			if (m_Context->m_Stack._data[i].m_Type > ValueType::NativeFunction)
			{
				if (m_Context->m_Stack._data[i].m_Object->m_Grey == false)
				{
					m_Context->m_Stack._data[i].m_Object->m_Grey = true;
					this->m_Greys.Push(m_Context->m_Stack._data[i]);
				}
			}
		}
	}


	//this is really part of the sweep section
	if (m_Context->m_CallStack.size() > 0)
	{
		//traverse all local vars
		if (m_Context->m_CurFrame && m_Context->m_CurFrame->m_Grey == false)
		{
			m_Context->m_CurFrame->m_Grey = true;
			this->m_Greys.Push(Value(m_Context->m_CurFrame));
		}

		int sp = 0;
		for (unsigned int i = 0; i < m_Context->m_CallStack.size(); i++)
		{
			auto closure = m_Context->m_CallStack[i].second;
			if (closure == 0)
				continue;

			if (closure->m_Grey == false)
			{
				closure->m_Grey = true;
				this->m_Greys.Push(Value(closure));
			}
			int max = sp+closure->m_Prototype->m_Locals;
			for (; sp < max; sp++)
			{
				if (m_Context->m_LocalStack[sp].m_Type > ValueType::NativeFunction)
				{
					if (m_Context->m_LocalStack[sp].m_Object->m_Grey == false)
					{
						m_Context->m_LocalStack[sp].m_Object->m_Grey = true;
						this->m_Greys.Push(m_Context->m_LocalStack[sp]);
					}
				}
			}
		}

		//mark curframe locals
		if (m_Context->m_CurFrame)
		{
			int max = sp+m_Context->m_CurFrame->m_Prototype->m_Locals;
			for (; sp < max; sp++)
			{
				if (m_Context->m_LocalStack[sp].m_Type > ValueType::NativeFunction)
				{
					if (m_Context->m_LocalStack[sp].m_Object->m_Grey == false)
					{
						m_Context->m_LocalStack[sp].m_Object->m_Grey = true;
						this->m_Greys.Push(m_Context->m_LocalStack[sp]);
					}
				}
			}
		}
	}

	{
		//StackProfile prof("Traverse Greys");
		while(this->m_Greys.size() > 0)
		{
			//traverse the object
			auto obj = this->m_Greys.Pop();
			switch (obj.m_Type)
			{
			case ValueType::Object:
				{
					//obj.m_Object->DebugPrint();
					if (obj.m_Object->m_Prototype && obj.m_Object->m_Prototype->m_Grey == false)
					{
						obj.m_Object->m_Prototype->m_Grey = true;

						this->m_Greys.Push(obj.m_Object->m_Prototype);
					}

					obj.m_Object->m_Mark = true;
					for (auto& ii: *obj.m_Object)
					{
						if (ii.first.m_Type > ValueType::NativeFunction && ii.first.m_Object->m_Grey == false)
						{
							ii.first.m_Object->m_Grey = true;
							m_Greys.Push(ii.first);
						}
						if (ii.second.m_Type > ValueType::NativeFunction && ii.second.m_Object->m_Grey == false)
						{
							ii.second.m_Object->m_Grey = true;
							m_Greys.Push(ii.second);
						}
					}
					break;
				}
			case ValueType::Array:
				{
					obj.m_Array->m_Mark = true;

					for (auto& ii: obj.m_Array->m_Data)
					{
						if (ii.m_Type > ValueType::NativeFunction && ii.m_Object->m_Grey == false)
						{
							ii.m_Object->m_Grey = true;
							m_Greys.Push(ii);
						}
					}

					break;
				}
			case ValueType::String:
				{
					obj.m_String->m_Mark = true;

					break;
				}
#ifdef _DEBUG
			case ValueType::Capture:
				{
					throw RuntimeException("There should not be an upvalue in the grey loop");
					break;
				}
#endif
			case ValueType::Function:
				{
					obj.m_Function->m_Mark = true;
					if (obj.m_Function->m_Prev && obj.m_Function->m_Prev->m_Grey == false)
					{
						obj.m_Function->m_Prev->m_Grey = true;
						m_Greys.Push(Value(obj.m_Function->m_Prev));
					}

					if (obj.m_Function->m_UpValueCount)
					{
						for (unsigned int i = 0; i < obj.m_Function->m_UpValueCount; i++)
						{
							auto uv = obj.m_Function->m_UpValues[i];
							if (uv && uv->m_Grey == false)
							{
								if (uv->m_Closed)
								{
									//mark the value stored in it
									if (uv->m_Value.m_Type > ValueType::NativeFunction)
									{
										if (uv->m_Value.m_Object->m_Grey == false)
										{
											uv->m_Value.m_Object->m_Grey = true;
											m_Greys.Push(uv->m_Value);
										}
									}
								}
								//mark it
								uv->m_Grey = true;
								uv->m_Mark = true;
							}
						}
					}

					if (obj.m_Function->m_Generator)
					{
						//mark generator stack
						for (unsigned int i = 0; i < obj.m_Function->m_Prototype->m_Locals; i++)
						{
							if (obj.m_Function->m_Generator->m_Stack[i].m_Type > ValueType::NativeFunction)
							{
								if (obj.m_Function->m_Generator->m_Stack[i].m_Object->m_Grey == false)
								{
									obj.m_Function->m_Generator->m_Stack[i].m_Object->m_Grey = true;
									m_Greys.Push(obj.m_Function->m_Generator->m_Stack[i]);
								}
							}
						}
					}
					break;
				}
			case ValueType::Userdata:
				{
					obj.m_UserData->m_Mark = true;

					if (obj.m_UserData->m_Prototype && obj.m_UserData->m_Prototype->m_Grey == false)
					{
						obj.m_UserData->m_Prototype->m_Grey = true;

						m_Greys.Push(obj.m_UserData->m_Prototype);
					}
					break;
				}
			}
		}
	}
}

void GarbageCollector::Sweep()
{
	bool nextIncremental = ((this->m_CollectionCounter+1)%GC_STEPS)!=0;
	bool incremental = ((this->m_CollectionCounter)%GC_STEPS)!=0;

	/* SWEEPING SECTION */

	//this must all be done when sweeping!!!!!!!

	//process stack variables, stack vars are ALWAYS grey

	//finally sweep through
	//sweep and free all whites and make all blacks white
	//iterate through all gc values
#ifdef _DEBUG
	if (this->m_Greys.size() > 0)
		throw RuntimeException("Runtime Error: Garbage collector grey array not empty when collecting!");
#endif

	if (!incremental)//do a gen2 collection
	{
		auto g2list = std::move(this->m_Generation2);
		this->m_Generation2.clear();
		for (auto& ii: g2list)
		{
			if (ii->mark || ii->refcount)
			{
				ii->mark = false;
				ii->grey = false;

				this->m_Generation2.push_back(ii);
			}
			else
			{
				this->Free(ii);
			}
		}
	}

	auto g1list = std::move(this->m_Generation1);
	this->m_Generation1.clear();
	for (auto& ii: g1list)
	{
		if (ii->mark || ii->refcount)
		{
			//ii->mark = false;//perhaps do this ONLY IF we just did a gen2 collection
			//ii->grey = false;
			this->m_Generation2.push_back(ii);//promote, it SURVIVED
		}
		else
		{
			this->Free(ii);
		}
	}

	//Obviously, this approach doesn't work for a non-copying GC. But the main insights behind a generational GC can be abstracted:

	//Minor collections only take care of newly allocated objects.
	//Major collections deal with all objects, but are run much less often.

	//The basic idea is to modify the sweep phase: 
	//1. free the (unreachable) white objects, but don't flip the color of black objects before a minor collection. 
	//2. The mark phase of the following minor collection then only traverses newly allocated blocks and objects written to (marked gray). 
	//3. All other objects are assumed to be still reachable during a minor GC and are neither traversed, nor swept, nor are their marks changed (kept black). A regular sweep phase is used if a major collection is to follow.
}

void GarbageCollector::Run()
{
	//printf("Running GC: %d Greys, %d Globals, %d Stack\n%d Closures, %d Arrays, %d Objects, %d Userdata\n", this->greys.size(), this->vars.size(), 0, this->closures.size(), this->arrays.size(), this->objects.size(), this->userdata.size());
#ifdef JET_TIME_EXECUTION
	INT64 start, end;
	//QueryPerformanceFrequency( (LARGE_INTEGER *)&rate );
	QueryPerformanceCounter( (LARGE_INTEGER *)&start );
#endif
	//mark all references in the grey stack
	this->Mark();

	//clear up dead memory
	this->Sweep();

	this->m_CollectionCounter++;//used to determine collection mode
#ifdef JET_TIME_EXECUTION
	INT64 rate;
	QueryPerformanceCounter( (LARGE_INTEGER *)&end );
	QueryPerformanceCounter((LARGE_INTEGER*)&rate);
	INT64 diff = end - start;
	double dt = ((double)diff)/((double)rate);

	printf("Took %lf seconds to collect garbage\n\n", dt);
#endif
	//printf("collection done\n");
	//printf("GC Complete: %d Greys, %d Globals, %d Stack\n%d Closures, %d Arrays, %d Objects, %d Userdata\n", this->greys.size(), this->vars.size(), 0, this->closures.size(), this->arrays.size(), this->objects.size(), this->userdata.size());
}

void GarbageCollector::Free(gcval* ii)
{
	switch (ii->type)
	{
	case ValueType::Function:
		{
			Closure* fun = (Closure*)ii;
#ifdef JETGCDEBUG
			printf("GC Freeing Function %d\n", ii);

			fun->m_Generator = (Generator*)0xcdcdcdcd;
#else
			if (fun->m_UpValueCount)
				delete[] fun->m_UpValues;

			delete fun->m_Generator;
			delete fun;
#endif
			break;
		}
	case ValueType::Object:
		{
#ifdef JETGCDEBUG
			JetObject* obj = (JetObject*)ii;
			obj->m_Nodes = (ObjNode*)0xcdcdcdcd;
#else
			delete (JetObject*)ii;
#endif
			break;
		}
	case ValueType::Array:
		{
#ifdef JETGCDEBUG
			JetArray* arr = (JetArray*)ii;
			arr->m_Data.clear();
#endif
			delete (JetArray*)ii;
			//#endif
			break;
		}
	case ValueType::Userdata:
		{
			Value ud = Value(((JetUserdata*)ii), ((JetUserdata*)ii)->m_Prototype);
			Value _gc = (*((JetUserdata*)ii)->m_Prototype).get("_gc");
			if (_gc.m_Type == ValueType::NativeFunction)
				_gc.m_NativeFunction(this->m_Context, &ud, 1);
			else if (_gc.m_Type == ValueType::Function)
				throw RuntimeException("Non Native _gc Hooks Not Implemented!");//todo
			else if (_gc.m_Type != ValueType::Null)
				throw RuntimeException("Invalid _gc Hook!");
			delete (JetUserdata*)ii;
			break;
		}
	case ValueType::String:
		{
			JetString* str = (JetString*)ii;
			delete[] str->m_Data;
			delete str;
			break;
		}
	case ValueType::Capture:
		{
			Capture* uv = (Capture*)ii;
			delete uv;
			break;
		}
#ifdef _DEBUG
	default:
		throw RuntimeException("Runtime Error: Invalid GC Object Typeid!");
#endif
	}
}