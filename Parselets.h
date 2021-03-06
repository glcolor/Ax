
#ifndef _JET_PARSELETS_HEADER
#define _JET_PARSELETS_HEADER

#include "Expressions.h"
#include "Token.h"

#include <stdlib.h>

namespace Jet
{
	enum Precedence {
		// Ordered in increasing precedence.
		ASSIGNMENT = 1,
		LOGICAL = 2,// || or &&
		CONDITIONAL = 3,
		BINARY = 4,
		SUM = 5,
		PRODUCT = 6,
		PREFIX = 7,
		POSTFIX = 8,
		CALL = 9,//was 9 before
	};

	class Parser;
	class Expression;

	//Parselets
	class PrefixParselet
	{
	public:
		virtual ~PrefixParselet() {};
		virtual Expression* parse(Parser* parser, Token token) = 0;
	};

	class NameParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class IntNumberParselet : public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token)
		{
			return new IntNumberExpression(::_atoi64(token.getText().c_str()));
		}
	};

	class RealNumberParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token)
		{
			return new RealNumberExpression(::atof(token.getText().c_str()));
		}
	};

	class NullParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token)
		{
			return new NullExpression();
		}
	};

	class LambdaParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class ArrayParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class ObjectParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class StringParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token)
		{
			return new StringExpression(token.getText());
		}
	};

	class GroupParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class PrefixOperatorParselet: public PrefixParselet
	{
		int precedence;
	public:
		PrefixOperatorParselet(int precedence)
		{
			this->precedence = precedence;
		}

		Expression* parse(Parser* parser, Token token);

		int GetPrecedence()
		{
			return precedence;
		}
	};

	class InfixParselet
	{
	public:
		virtual ~InfixParselet() {};
		virtual Expression* parse(Parser* parser, Expression* left, Token token) = 0;

		virtual int getPrecedence() = 0;
	};

	class AssignParselet: public InfixParselet
	{
	public:
		Expression* parse(Parser* parser, Expression* left, Token token);

		int getPrecedence()
		{
			return Precedence::ASSIGNMENT;
		}
	};

	class OperatorAssignParselet: public InfixParselet
	{
	public:

		Expression* parse(Parser* parser, Expression* left, Token token);

		int getPrecedence()
		{
			return Precedence::ASSIGNMENT;
		}
	};

	class SwapParselet: public InfixParselet
	{
	public:
		Expression* parse(Parser* parser, Expression* left, Token token);

		int getPrecedence()
		{
			return Precedence::ASSIGNMENT;
		}
	};

	class PostfixOperatorParselet: public InfixParselet
	{
		int precedence;
	public:
		PostfixOperatorParselet(int precedence)
		{
			this->precedence = precedence;
		}

		Expression* parse(Parser* parser, Expression* left, Token token)
		{
			return new PostfixExpression(left, token);
		}

		int getPrecedence()
		{
			return precedence;
		}
	};

	class BinaryOperatorParselet: public InfixParselet
	{
		int precedence;
		bool isRight;
	public:
		BinaryOperatorParselet(int precedence, bool isRight)
		{
			this->precedence = precedence;
			this->isRight = isRight;
		}

		Expression* parse(Parser* parser, Expression* left, Token token);

		int getPrecedence()
		{
			return precedence;
		}
	};

	class IndexParselet: public InfixParselet
	{
	public:
		Expression* parse(Parser* parser, Expression* left, Token token);

		int getPrecedence()
		{
			//replace precedence here
			return 9;
		}
	};

	class MemberParselet: public InfixParselet
	{
	public:
		Expression* parse(Parser* parser, Expression* left, Token token);

		int getPrecedence()
		{
			//replace precedence here
			return 3;//maybe?
		}
	};

	class CallParselet: public InfixParselet
	{
	public:

		Expression* parse(Parser* parser, Expression* left, Token token);

		int getPrecedence()
		{
			return Precedence::CALL;//whatever postfix precedence is
		}
	};

	class StatementParselet
	{
	public:
		bool TrailingSemicolon;
		StatementParselet() { TrailingSemicolon = false;}
		virtual ~StatementParselet() {};
		virtual Expression* parse(Parser* parser, Token token) = 0;
	};

	class ReturnParselet: public StatementParselet
	{
	public:
		ReturnParselet()
		{
			this->TrailingSemicolon = true;
		}

		Expression* parse(Parser* parser, Token token);
	};

	class ContinueParselet: public StatementParselet
	{
	public:
		ContinueParselet()
		{
			this->TrailingSemicolon = true;
		}

		Expression* parse(Parser* parser, Token token)
		{
			return new ContinueExpression();
		}
	};

	class BreakParselet: public StatementParselet
	{
	public:
		BreakParselet()
		{
			this->TrailingSemicolon = true;
		}

		Expression* parse(Parser* parser, Token token)
		{
			return new BreakExpression();
		}
	};

	class WhileParselet: public StatementParselet
	{
	public:
		WhileParselet()
		{
			this->TrailingSemicolon = false;
		}

		Expression* parse(Parser* parser, Token token);
	};

	class FunctionParselet: public StatementParselet
	{
	public:
		FunctionParselet()
		{
			this->TrailingSemicolon = false;
		}

		Expression* parse(Parser* parser, Token token);
	};

	class IfParselet: public StatementParselet
	{
	public:
		IfParselet()
		{
			this->TrailingSemicolon = false;
		}

		Expression* parse(Parser* parser, Token token);
	};

	class ForParselet: public StatementParselet
	{
	public:
		ForParselet()
		{
			this->TrailingSemicolon = false;
		}

		Expression* parse(Parser* parser, Token token);
	};

	class LocalParselet: public StatementParselet
	{
	public:
		LocalParselet()
		{
			this->TrailingSemicolon = false;
		}

		Expression* parse(Parser* parser, Token token);
	};

	class GlobalParselet : public StatementParselet
	{
	public:
		GlobalParselet()
		{
			this->TrailingSemicolon = false;
		}

		Expression* parse(Parser* parser, Token token);
	};

	//use me for parallelism
	class ConstParselet: public StatementParselet
	{
	public:
		ConstParselet()
		{
			this->TrailingSemicolon = true;
		}

		Expression* parse(Parser* parser, Token token);
	};

	class YieldParselet: public StatementParselet
	{
	public:
		YieldParselet()
		{
			this->TrailingSemicolon = true;
		}
		Expression* parse(Parser* parser, Token token);
	};

	class InlineYieldParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class ResumeParselet: public StatementParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class ResumePrefixParselet: public PrefixParselet
	{
	public:
		Expression* parse(Parser* parser, Token token);
	};

	class ClassParselet :public StatementParselet
	{
		std::string									m_Name;
		std::string									m_Base;
		std::map<std::string,FunctionExpression*>	m_Functions;
		std::vector<VarDefine>						m_Fields;
	public:
		ClassParselet()
		{
			this->TrailingSemicolon = false;
		}

		Expression* parse(Parser* parser, Token token);

		//是否拥有指定名字的字段或函数
		bool IsExist(const std::string& name) const
		{
			auto i = m_Functions.find(name);
			if (i != m_Functions.end()) return true;
			for (auto& v:m_Fields)
			{
				if (name==v.m_Name.text) return true;
			}
			return false;
		}

		//解析字段
		void ParseFields(Parser* parser);

		//解析函数
		void ParseFunction(Parser* parser, const Token& token);
	};
};

#endif