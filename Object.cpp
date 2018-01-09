#include "Value.h"
#include "JetContext.h"

using namespace Jet;

size_t stringhash(const char* str)
{
	/*size_t hash = *str;
	while (*(str++))
	hash += *str;*/

	size_t hash = 5381;
	char c;
	while (c = *str++)
		hash = ((hash << 5) + hash) + c;
	return hash;
}

JetObject::JetObject(JetContext* jcontext)
{
	m_Grey = this->m_Mark = false;
	m_RefCount = 0;
	m_Type = ValueType::Object;

	m_Prototype = jcontext->m_ObjectPrototype;
	m_Context = jcontext;
	m_Size = 0;
	m_NodeCount = 2;
	m_Nodes = new ObjNode[2];
}

JetObject::~JetObject()
{
	delete[] m_Nodes;
}

std::size_t JetObject::key(const Value* v) const
{
	switch(v->m_Type)
	{
	case ValueType::Null:
		return 0;
	case ValueType::Array:
	case ValueType::Userdata:
	case ValueType::Function:
	case ValueType::Object:
		return (size_t)v->m_Array;
	case ValueType::Int:
		return (size_t)v->m_IntValue;
	case ValueType::Real:
		return (size_t)v->m_RealValue;
	case ValueType::String:
		return stringhash(v->m_String->m_Data);
	case ValueType::NativeFunction:
		return (size_t)v->m_NativeFunction;
	}
	return 0;
}

//just looks for a node
ObjNode* JetObject::findNode(const Value* key)
{
	ObjNode* node = &this->m_Nodes[this->key(key)%this->m_NodeCount];
	if (node->first.m_Type != ValueType::Null)
	{
		do
		{
			if (node->first == *key)
				return node;//we found it
			else
				node = node->next;
		}
		while(node);
	}	
	return 0;
}

ObjNode* JetObject::findNode(const char* key)
{
	ObjNode* node = &this->m_Nodes[stringhash(key)%this->m_NodeCount];
	if (node->first.m_Type != ValueType::Null)
	{
		do
		{
			if (node->first.m_Type == ValueType::String && strcmp(node->first.m_String->m_Data, key) == 0)
				return node;//we found it
			else
				node = node->next;
		}
		while(node);
	}	
	return 0;
}

//finds node for key or creates one if doesnt exist
ObjNode* JetObject::getNode(const Value* key)
{
	ObjNode* mpnode = &this->m_Nodes[this->key(key)%this->m_NodeCount];//node in the main position
	if (mpnode->first.m_Type != ValueType::Null)
	{
		ObjNode* node = mpnode;
		while(node->next)//go down the linked list of nodes
		{
			if (node->first == *key)
				return node;//we found it
			else
				node = node->next;
		}
		if (node->first == *key)
			return node;//we found it

		if (this->m_Size == this->m_NodeCount)//regrow if we are out of room
		{
			this->resize();
			return this->getNode(key);
		}

		ObjNode* onode = &this->m_Nodes[this->key(&mpnode->first)%this->m_NodeCount];//get the main position of the node in our main position

		ObjNode* newnode = this->getFreePosition();//find an open node to insert into

		if (onode != mpnode)//is this node not in the main position?
		{
			//relocate the node and relink it in, then insert the new node at its position
			while (onode->next != mpnode) onode = onode->next;

			this->Barrier();

			onode->next = newnode;
			*newnode = *mpnode;
			mpnode->next = 0;
			mpnode->second = Value();
			mpnode->first = *key;
			m_Size++;

			return mpnode;
		}
		else
		{
			this->Barrier();

			//insert the new node and just link in, we had a hash collision
			newnode->first = *key;
			newnode->next = 0;
			node->next = newnode;//link the new one into the chain
			m_Size++;

			return newnode;
		}
	}

	this->Barrier();

	//main position for key is open, just insert
	mpnode->first = *key;
	mpnode->next = 0;
	m_Size++;

	return mpnode;
}

//this method allocates new key strings
ObjNode* JetObject::getNode(const char* key)
{
	ObjNode* mpnode = &this->m_Nodes[stringhash(key)%this->m_NodeCount];//node in the main position
	if (mpnode->first.m_Type != ValueType::Null)
	{
		ObjNode* node = mpnode;
		while (node->next)
		{
			if (node->first.m_Type == ValueType::String && strcmp(node->first.m_String->m_Data, key) == 0)
				return node;//we found it
			else
				node = node->next;
		}

		if (node->first.m_Type == ValueType::String && strcmp(node->first.m_String->m_Data, key) == 0)
			return node;//we found it

		if (this->m_Size == this->m_NodeCount)//regrow if we are out of room
		{
			this->resize();
			return this->getNode(key);
		}

		ObjNode* onode = &this->m_Nodes[this->key(&mpnode->first)%this->m_NodeCount];//get the main position of the node in our main position

		ObjNode* newnode = this->getFreePosition();//find an open node to insert into

		if (onode != mpnode)//is this node not in the main position?
		{
			//relocate the node and relink it in, then insert the new node at its position
			while (onode->next != mpnode) onode = onode->next;

			this->Barrier();

			onode->next = newnode;
			*newnode = *mpnode;
			mpnode->next = 0;
			mpnode->second = Value();
			mpnode->first = m_Context->CreateNewString(key);
			m_Size++;

			return mpnode;
		}
		else
		{
			this->Barrier();

			//insert the new node and just link in, we had a hash collision
			newnode->first = m_Context->CreateNewString(key);
			newnode->next = 0;
			node->next = newnode;//link the new one into the chain
			m_Size++;

			return newnode;
		}
	}

	this->Barrier();

	//main position for key is open, just insert
	mpnode->first = m_Context->CreateNewString(key);
	mpnode->next = 0;
	m_Size++;

	return mpnode;
}

ObjNode* JetObject::getFreePosition()
{
	for (unsigned int i = 0; i < this->m_NodeCount; i++)
	{
		if (this->m_Nodes[i].first.m_Type == ValueType::Null)
			return &this->m_Nodes[i];
	}
	return 0;
}

void JetObject::resize()
{
	//change resize rate, this is just a quickie
	auto t = this->m_Nodes;
	this->m_Nodes = new ObjNode[this->m_NodeCount*2];//lets be dumb atm
	auto osize = this->m_NodeCount;
	this->m_NodeCount *= 2;
	this->m_Size = 0;//cheat
	for (unsigned int i = 0; i < osize; i++)
	{
		//reinsert into hashtable
		ObjNode* n = &t[i];

		ObjNode* np = this->getNode(&n->first);
		np->second = n->second;
	}

	delete[] t;
}

//try not to use these in the vm
Value& JetObject::operator [](const Value& key)
{
	ObjNode* node = this->getNode(&key);
	return node->second;
}

//special operator for strings to deal with insertions
Value& JetObject::operator [](const char* key)
{
	ObjNode* node = this->getNode(key);
	return node->second;
}

void JetObject::DebugPrint()
{
	printf("JetObject Changed:\n");
	for (unsigned int i = 0; i < this->m_NodeCount; i++)
	{
		auto k = this->m_Nodes[i].first.ToString();
		auto v = this->m_Nodes[i].second.ToString();
		printf("[%d] %s    %s   Hash: %i\n", i, k.c_str(), v.c_str(), (this->key(&this->m_Nodes[i].first)%this->m_NodeCount));
	}
}

//use this function
void JetObject::Barrier()
{
	if (this->m_Mark)
	{
		//reset to grey and push back for reprocessing
		this->m_Mark = false;
		this->m_Context->m_GC.m_Greys.Push(this);//push to grey stack
	}
}