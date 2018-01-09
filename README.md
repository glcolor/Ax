 Jx
==========

Jx是一个实验性的脚本编程语言，其代码基于[JetScript](https://github.com/matt-attack/JetScript)修改而成。

### 示例
```cpp
fun fibo(n)
{
	if (n < 2)
		return n;
	else
		return fibo(n-1)+fibo(n-2);
}
print("Fibonacci of 10: ", fibo(10));
```



### 在C++中使用:
```cpp
#include <JetContext.h>

Jet::JetContext context;
try
{
	Jet::Value return = context.Script("return 7;");
}
catch (CompilerException e)
{
	//an exception occured while compiling
}
```

### 数据类型
- 整数
```cpp
number = 256;
number = 3;
```
- 浮点数
```cpp
number = 1.0;
number = 3.1415926535895;
```
- 字符串
```cpp
string = "hello";
```
- 对象 （基于Hash映射表）
```cpp
//how to define objects
obj = {};
obj2 = { hey = 1, apple = 2, "name":"value" };
//two different ways to index items in the object
obj.apple = 2;
obj["apple2"] = 3;
```
- 数组
```cpp
//how to define arrays
arr = [];
arr2 = [1,2,3,4,5,6];

arr:resize(2);
arr[0] = 255;
arr[1] = "Apples";
```
- Userdata - used for native user defined types, stores a void*

### C++ 绑定
You can bind functions in C++ to Jet as well as bind native types through  userdata types and metatables.

- 绑定函数到脚本
```cpp
Jet::JetContext context;
context["myfunction"] = [](JetContext* context, Value* arguments, int numarguments)
{
	printf("Hello from C++!");
};
context.Script("myfunction();");
```
Outputs "Hello from C++!" to console.


- 绑定自定义类型
```cpp
Jet::JetContext context;
auto meta = context.NewPrototype("TypeName");//this is a list of all meta-methods you want to add
meta["t1"] = [](JetContext* context, Value* v, int args)
{
	printf("Hi from metatable!");
};

context["x"] = context.NewUserdata(0/*any native data you want associated*/, meta);
auto out = context.Script("x.t1();");
```
Outputs "Hi from metatable!" to the console.