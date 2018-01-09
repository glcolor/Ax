#ifndef _LANG_CONTEXT_HEADER
#define _LANG_CONTEXT_HEADER

#include <functional>
#include <string>
#include <map>
#include <algorithm>
#include "Value.h"
#include "VMStack.h"
#include "Parser.h"
#include "JetInstructions.h"
#include "JetExceptions.h"
#include "GarbageCollector.h"


#ifdef _WIN32
#include <Windows.h>
//#define JET_TIME_EXECUTION
#endif

//GC������ֵ
#define GC_INTERVAL 200	

//��0���ڴ��������1���ڴ�������ת����ֵ(��0���ھ������ٴ��ռ�������Ȼ����תΪ��1��������
#define GC_STEPS 4		

#define JET_STACK_SIZE 1024
#define JET_MAX_CALLDEPTH 1024

namespace Jet
{
	typedef std::function<void(Jet::JetContext*,Jet::Value*,int)> JetFunction;
#define JetBind(context, fun) 	auto temp__bind_##fun = [](Jet::JetContext* context,Jet::Value* args, int numargs) { return Value(fun(args[0]));};context[#fun] = Jet::Value(temp__bind_##fun);
#define JetBind2(context, fun) 	auto temp__bind_##fun = [](Jet::JetContext* context,Jet::Value* args, int numargs) {  return Value(fun(args[0],args[1]));};context[#fun] = Jet::Value(temp__bind_##fun);
#define JetBind3(context, fun, type) 	auto temp__bind_##fun = [](Jet::JetContext* context,Jet::Value* args, int numargs) {  return Value(fun((type)args[0],(type)args[1]));};context[#fun] = Jet::Value(temp__bind_##fun);

	//���ú���
	Value gc(JetContext* context,Value* args, int numargs);
	Value print(JetContext* context,Value* args, int numargs);
	Value tostring(JetContext* context, Value* args, int numargs);
	Value toint(JetContext* context, Value* args, int numargs);
	Value toreal(JetContext* context, Value* args, int numargs);
	
	//��Ϣ��������Ķ���
	typedef int (__cdecl *OutputFunction) (const char* format, ...);

	/// <summary>
	/// �ű�ִ��������(�����)
	/// </summary>
	class JetContext
	{
		friend struct Generator;
		friend struct Value;
		friend class JetObject;
		friend class GarbageCollector;		
	public:
		//���캯��
		JetContext();
		//��������
		~JetContext();

		/// <summary>
		/// �����¶���
		/// </summary>
		/// <returns>�����Ķ���</returns>
		Value CreateNewObject();

		/// <summary>
		/// ����������
		/// </summary>
		/// <returns>����������</returns>
		Value CreateNewArray();

		/// <summary>
		/// �������Զ�������
		/// </summary>
		/// <param name="data">�Զ������ݵ�ָ��</param>
		/// <param name="proto">�Զ������ݵ�ԭ��</param>
		/// <returns>�������Զ�������</returns>
		Value CreateNewUserData(void* data, const Value& proto);

		/// <summary>
		/// �������ַ���
		/// </summary>
		/// <param name="string">�ַ�������0��β��</param>
		/// <param name="copy">�Ƿ������ݣ���������ƣ���ֱ��ʹ�ò����ṩ���ַ���</param>
		/// <returns>�������ַ���</returns>
		Value CreateNewString(const char* string, bool copy = true);
		
		/// <summary>
		/// ���һ���Զ�������ԭ��
		/// </summary>
		/// <param name="Typename">ԭ�͵�����</param>
		/// <returns>ԭ�Ͷ���</returns>
		Value AddPrototype(const char* Typename);

		/// <summary>
		/// ���һ��������
		/// </summary>
		/// <param name="name">����</param>
		/// <param name="library">�������п�ִ�д���Ķ���(Assemble�����ķ���ֵ)</param>
		void AddLibrary(const std::string& name, Value& library)
		{
			library.AddRef();
			this->m_Libraries[name] = library;
		}		

		//����ȫ�ֱ����Ľӿ�
		Value&		operator[](const std::string& id);
		Value		Get(const std::string& name);
		void		Set(const std::string& name, const Value& value);

		//ֱ��ִ��ָ���Ĵ���
		std::string Script(const std::string& code, const std::string& filename = "file");
		Value		Script(const char* code, const char* filename = "file");

		//��ָ���������Ϊ���ָ��
		std::vector<IntermediateInstruction> Compile(const char* code, const char* filename = "file");

		//�����ָ�����Ϊ��ִ�к���
		Value	Assemble(const std::vector<IntermediateInstruction>& code);

		//����ָ���ĺ���
		Value	Call(const char* m_FunctionPrototype, Value* args = 0, unsigned int numargs = 0);
		Value	Call(const Value* m_FunctionPrototype, Value* args = 0, unsigned int numargs = 0);

		//ִ��һ���ڴ���������
		void	RunGC();

		//��ȡ��������Ϣ�������
		OutputFunction GetOutputFunction() const		{ return m_OutputFunction; }
		void	SetOutputFunction(OutputFunction val);
	private:
		//��ʼִ�� iptr ����ָ��
		Value Execute(int iptr, Closure* frame);
		//VM�ڲ�ʹ�õĺ�������
		unsigned int Call(const Value* m_FunctionPrototype, unsigned int iptr, unsigned int args);

		//�����õĺ���
		void GetCode(int ptr, Closure* closure, std::string& ret, unsigned int& line);

		//׷�ٶ�ջ
		void StackTrace(int curiptr, Closure* cframe);

		//��ӡ����ջ
		static Value Callstack(JetContext* context, Value* v, int ar);
	private:
		//����ջ
		VMStack<Value>								m_Stack;
		//����ջ
		VMStack<std::pair<unsigned int, Closure*> > m_CallStack;

		//����
		std::unordered_map<std::string, Function*>	m_Functions;

		//��ڵ�
		std::vector<Function*>						m_EntryPoints;

		//����
		std::vector<Value>							m_Variables;

		//����������������
		std::unordered_map<std::string, unsigned int> m_VariableIndex;

		//������������
		CompilerContext		m_Compiler;

		// �������͵�ԭ��
		JetObject*			m_StringPrototype;
		JetObject*			m_ArrayPrototype;
		JetObject*			m_ObjectPrototype;
		JetObject*			m_ArrayIterPrototype;
		JetObject*			m_ObjectIterPrototype;
		JetObject*			m_FunctionPrototype;

		//requireָ��Ļ�����
		std::map<std::string, Value> m_RequireCache;
		//����Ŀ�
		std::map<std::string, Value> m_Libraries;

		//�ڴ������
		GarbageCollector		m_GC;

		//ע�ᵽ�������ԭ��
		std::vector<JetObject*> m_Prototypes;

		//�����ӵıհ�
		Closure*				m_LastAdded;

		struct OpenCapture
		{
			Capture* capture;
#ifdef _DEBUG
			Closure* creator;
#endif
		};
		std::deque<OpenCapture> m_OpenCaptures;

		//��ǰ�հ�
		Closure*	m_CurFrame;

		//�ֲ�����ջָ��stack pointer
		Value*		m_SP;
		//�ֲ�����ջ�����ڱ���ֲ�����?
		Value		m_LocalStack[JET_STACK_SIZE];

		//�������ָ��
		OutputFunction	m_OutputFunction = printf;		
	};
}

#endif