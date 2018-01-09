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

//GC触发阈值
#define GC_INTERVAL 200	

//第0代内存垃圾与第1代内存垃圾的转换阈值(第0代在经历多少次收集后若仍然存活，则转为第1代垃圾）
#define GC_STEPS 4		

#define JET_STACK_SIZE 1024
#define JET_MAX_CALLDEPTH 1024

namespace Jet
{
	typedef std::function<void(Jet::JetContext*,Jet::Value*,int)> JetFunction;
#define JetBind(context, fun) 	auto temp__bind_##fun = [](Jet::JetContext* context,Jet::Value* args, int numargs) { return Value(fun(args[0]));};context[#fun] = Jet::Value(temp__bind_##fun);
#define JetBind2(context, fun) 	auto temp__bind_##fun = [](Jet::JetContext* context,Jet::Value* args, int numargs) {  return Value(fun(args[0],args[1]));};context[#fun] = Jet::Value(temp__bind_##fun);
#define JetBind3(context, fun, type) 	auto temp__bind_##fun = [](Jet::JetContext* context,Jet::Value* args, int numargs) {  return Value(fun((type)args[0],(type)args[1]));};context[#fun] = Jet::Value(temp__bind_##fun);

	//内置函数
	Value gc(JetContext* context,Value* args, int numargs);
	Value print(JetContext* context,Value* args, int numargs);
	Value tostring(JetContext* context, Value* args, int numargs);
	Value toint(JetContext* context, Value* args, int numargs);
	Value toreal(JetContext* context, Value* args, int numargs);
	
	//信息输出函数的定义
	typedef int (__cdecl *OutputFunction) (const char* format, ...);

	/// <summary>
	/// 脚本执行上下文(虚拟机)
	/// </summary>
	class JetContext
	{
		friend struct Generator;
		friend struct Value;
		friend class JetObject;
		friend class GarbageCollector;		
	public:
		//构造函数
		JetContext();
		//析构函数
		~JetContext();

		/// <summary>
		/// 创建新对象
		/// </summary>
		/// <returns>创建的对象</returns>
		Value CreateNewObject();

		/// <summary>
		/// 创建新数组
		/// </summary>
		/// <returns>创建的数组</returns>
		Value CreateNewArray();

		/// <summary>
		/// 创建新自定义数据
		/// </summary>
		/// <param name="data">自定义数据的指针</param>
		/// <param name="proto">自定义数据的原型</param>
		/// <returns>创建的自定义数据</returns>
		Value CreateNewUserData(void* data, const Value& proto);

		/// <summary>
		/// 创建新字符串
		/// </summary>
		/// <param name="string">字符串（以0结尾）</param>
		/// <param name="copy">是否复制数据，如果不复制，则直接使用参数提供的字符串</param>
		/// <returns>创建的字符串</returns>
		Value CreateNewString(const char* string, bool copy = true);
		
		/// <summary>
		/// 添加一个自定义数据原型
		/// </summary>
		/// <param name="Typename">原型的名字</param>
		/// <returns>原型对象</returns>
		Value AddPrototype(const char* Typename);

		/// <summary>
		/// 添加一个函数库
		/// </summary>
		/// <param name="name">名字</param>
		/// <param name="library">包含所有可执行代码的对象(Assemble方法的返回值)</param>
		void AddLibrary(const std::string& name, Value& library)
		{
			library.AddRef();
			this->m_Libraries[name] = library;
		}		

		//访问全局变量的接口
		Value&		operator[](const std::string& id);
		Value		Get(const std::string& name);
		void		Set(const std::string& name, const Value& value);

		//直接执行指定的代码
		std::string Script(const std::string& code, const std::string& filename = "file");
		Value		Script(const char* code, const char* filename = "file");

		//将指定代码编译为汇编指令
		std::vector<IntermediateInstruction> Compile(const char* code, const char* filename = "file");

		//将汇编指令编译为可执行函数
		Value	Assemble(const std::vector<IntermediateInstruction>& code);

		//调用指定的函数
		Value	Call(const char* m_FunctionPrototype, Value* args = 0, unsigned int numargs = 0);
		Value	Call(const Value* m_FunctionPrototype, Value* args = 0, unsigned int numargs = 0);

		//执行一次内存垃圾回收
		void	RunGC();

		//获取和设置信息输出函数
		OutputFunction GetOutputFunction() const		{ return m_OutputFunction; }
		void	SetOutputFunction(OutputFunction val);
	private:
		//开始执行 iptr 处的指令
		Value Execute(int iptr, Closure* frame);
		//VM内部使用的函数调用
		unsigned int Call(const Value* m_FunctionPrototype, unsigned int iptr, unsigned int args);

		//调试用的函数
		void GetCode(int ptr, Closure* closure, std::string& ret, unsigned int& line);

		//追踪堆栈
		void StackTrace(int curiptr, Closure* cframe);

		//打印调用栈
		static Value Callstack(JetContext* context, Value* v, int ar);
	private:
		//数据栈
		VMStack<Value>								m_Stack;
		//调用栈
		VMStack<std::pair<unsigned int, Closure*> > m_CallStack;

		//函数
		std::unordered_map<std::string, Function*>	m_Functions;

		//入口点
		std::vector<Function*>						m_EntryPoints;

		//变量
		std::vector<Value>							m_Variables;

		//变量的名称索引表
		std::unordered_map<std::string, unsigned int> m_VariableIndex;

		//编译器上下文
		CompilerContext		m_Compiler;

		// 基础类型的原型
		JetObject*			m_StringPrototype;
		JetObject*			m_ArrayPrototype;
		JetObject*			m_ObjectPrototype;
		JetObject*			m_ArrayIterPrototype;
		JetObject*			m_ObjectIterPrototype;
		JetObject*			m_FunctionPrototype;

		//require指令的缓冲区
		std::map<std::string, Value> m_RequireCache;
		//导入的库
		std::map<std::string, Value> m_Libraries;

		//内存管理器
		GarbageCollector		m_GC;

		//注册到虚拟机的原型
		std::vector<JetObject*> m_Prototypes;

		//最近添加的闭包
		Closure*				m_LastAdded;

		struct OpenCapture
		{
			Capture* capture;
#ifdef _DEBUG
			Closure* creator;
#endif
		};
		std::deque<OpenCapture> m_OpenCaptures;

		//当前闭包
		Closure*	m_CurFrame;

		//局部变量栈指针stack pointer
		Value*		m_SP;
		//局部变量栈，用于保存局部变量?
		Value		m_LocalStack[JET_STACK_SIZE];

		//输出函数指针
		OutputFunction	m_OutputFunction = printf;		
	};
}

#endif