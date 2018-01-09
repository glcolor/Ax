#ifndef _JET_GC_HEADER
#define _JET_GC_HEADER

#include "Value.h"
#include "VMStack.h"
#include <vector>

namespace Jet
{
	class JetContext;

	class GarbageCollector
	{
		friend class JetObject;
		friend struct Value;
		//must free with GCFree, pointer is a bit offset to leave room for the flag

		JetContext* m_Context;
	public:
		//garbage collector stuff
		//need to unify everything except userdata
		struct gcval
		{
			bool			mark;
			bool			grey;
			Jet::ValueType	type;
			unsigned char	refcount;
		};
		std::vector<gcval*> m_Generation1;
		std::vector<gcval*> m_Generation2;
		//std::vector<gcval*> gen3;basically permanent objects, todo

		std::vector<Value>	m_NativeRefs;//a list of all objects stored natively to mark

		int					m_AllocationCounter;//used to determine when to run the GC
		int					m_CollectionCounter;//state of the gc
		VMStack<Value>		m_Greys;//stack of grey objects for processing

		GarbageCollector(JetContext* context);

		void Cleanup();

		inline void AddObject(gcval* obj)
		{
			this->m_Generation1.push_back(obj);
		}

		template<class T> 
		T* New()
		{
			//need to call constructor
			T* buf = new T();
			this->m_Generation1.push_back((gcval*)buf);
			return (T*)(buf);
		}

		//hack for objects
		template<class T> 
		T* New(JetContext* arg1)
		{
			//need to call constructor
			T* buf = new T(arg1);
			this->m_Generation1.push_back((gcval*)buf);
			return (T*)(buf);
		}

		//hack for strings
		template<class T> 
		T* New(char* arg1)
		{
			//need to call constructor
			T* buf = new T(arg1);
			this->m_Generation1.push_back((gcval*)buf);
			return (T*)(buf);
		}

		//hack for userdata
		template<class T> 
		T* New(void* arg1, JetObject* arg2)
		{
			//need to call constructor
			T* buf = new T(arg1, arg2);
			this->m_Generation1.push_back((gcval*)buf);
			//new (buf) T(arg1, arg2);
			return (T*)(buf);

#ifdef _DEBUG
#ifndef DBG_NEW      
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )     
#define new DBG_NEW   
#endif
#endif
		}

		void Run();

	private:
		void Mark();
		void Sweep();

		void Free(gcval* val);
	};
}
#endif
