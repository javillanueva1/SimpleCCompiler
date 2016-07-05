/*
 * File:	parser.c
 *
 * Description:	This file contains the public and private function and
 *		variable definitions for the recursive-descent parser for
 *		Simple C.
 */

# include <cstdlib>
# include <iostream>
# include "lexer.h"
# include "tokens.h"
# include "checker.h"
# include "generator.h"

using namespace std;

static int lookahead, nexttoken;
static string lexbuf, nextbuf;

static Type returnType;
static Expression *expression();
static Statement *statement();

static Symbols globals;


/*
 * Function:	error
 *
 * Description:	Report a syntax error to standard error.
 */

static void error()
{
    if (lookahead == DONE)
	report("syntax error at end of file");
    else
	report("syntax error at '%s'", lexbuf);

    exit(EXIT_FAILURE);
}


/*
 * Function:	match
 *
 * Description:	Match the next token against the specified token.  A
 *		failure indicates a syntax error and will terminate the
 *		program since our parser does not do error recovery.
 */

static void match(int t)
{
    if (lookahead != t)
	error();

    if (nexttoken) {
	lookahead = nexttoken;
	lexbuf = nextbuf;
	nexttoken = 0;
    } else
	lookahead = lexan(lexbuf);
}


/*
 * Function:	peek
 *
 * Description:	Return the next token in the input stream and save it so
 *		that match() will later return it.
 */

static int peek()
{
    if (!nexttoken)
	nexttoken = lexan(nextbuf);

    return nexttoken;
}


/*
 * Function:	expect
 *
 * Description:	Match the next token against the specified token, and
 *		return its lexeme.  We must save the contents of the buffer
 *		from the lexical analyzer before matching, since matching
 *		will advance to the next token.
 */

static string expect(int t)
{
    string buf = lexbuf;
    match(t);
    return buf;
}


/*
 * Function:	number
 *
 * Description:	Match the next token as a number and return its value.
 */

static unsigned long number()
{
    return strtoul(expect(NUM).c_str(), NULL, 0);
}


/*
 * Function:	isSpecifier
 *
 * Description:	Return whether the given token is a type specifier.
 */

static bool isSpecifier(int token)
{
    return token == INT || token == CHAR || token == STRUCT;
}


/*
 * Function:	specifier
 *
 * Description:	Parse a type specifier.  Simple C has only ints, chars, and
 *		structure types.
 *
 *		specifier:
 *		  int
 *		  char
 *		  struct identifier
 */

static string specifier()
{
    if (lookahead == INT || lookahead == CHAR)
	return expect(lookahead);

    match(STRUCT);
    return expect(ID);
}


/*
 * Function:	pointers
 *
 * Description:	Parse pointer declarators (i.e., zero or more asterisks).
 *
 *		pointers:
 *		  empty
 *		  * pointers
 */

static unsigned pointers()
{
    unsigned count = 0;


    while (lookahead == '*') {
	match('*');
	count ++;
    }

    return count;
}


/*
 * Function:	declarator
 *
 * Description:	Parse a declarator, which in Simple C is either a scalar
 *		variable or an array, with optional pointer declarators.
 *
 *		declarator:
 *		  pointers identifier
 *		  pointers identifier [ num ]
 */

static void declarator(const string &typespec)
{
    unsigned indirection;
    string name;


    indirection = pointers();
    name = expect(ID);

    if (lookahead == '[') {
	match('[');
	declareVariable(name, Type(typespec, indirection, number()));
	match(']');

    } else
	declareVariable(name, Type(typespec, indirection));
}


/*
 * Function:	declaration
 *
 * Description:	Parse a local variable declaration.  Global declarations
 *		are handled separately since we need to detect a function
 *		as a special case.
 *
 *		declaration:
 *		  specifier declarator-list ';'
 *
 *		declarator-list:
 *		  declarator
 *		  declarator , declarator-list
 */

static void declaration()
{
    string typespec;


    typespec = specifier();
    declarator(typespec);

    while (lookahead == ',') {
	match(',');
	declarator(typespec);
    }

    match(';');
}


/*
 * Function:	declarations
 *
 * Description:	Parse a possibly empty sequence of declarations.
 *
 *		declarations:
 *		  empty
 *		  declaration declarations
 */

static void declarations()
{
    while (isSpecifier(lookahead))
	declaration();
}


/*
 * Function:	primaryExpression
 *
 * Description:	Parse a primary expression.
 *
 *		primary-expression:
 *		  ( expression )
 *		  identifier ( expression-list )
 *		  identifier ( )
 *		  identifier
 *		  character
 *		  string
 *		  num
 *
 *		expression-list:
 *		  expression
 *		  expression , expression-list
 */

static Expression *primaryExpression()
{
    Symbol *symbol;
    Expression *expr;
    Expressions args;


    if (lookahead == '(') {
	match('(');
	expr = expression();
	match(')');

    } else if (lookahead == CHARACTER) {
	expr = new Character(expect(CHARACTER));

    } else if (lookahead == STRING) {
	expr = new String(expect(STRING));

    } else if (lookahead == NUM) {
	expr = new Number(expect(NUM));

    } else if (lookahead == ID) {
	symbol = checkIdentifier(expect(ID));

	if (lookahead == '(') {
	    match('(');

	    if (lookahead != ')') {
		args.push_back(expression());

		while (lookahead == ',') {
		    match(',');
		    args.push_back(expression());
		}
	    }

	    expr = checkCall(symbol, args);
	    match(')');

	} else
	    expr = new Identifier(symbol);

    } else {
	expr = nullptr;
	error();
    }

    return expr;
}


/*
 * Function:	postfixExpression
 *
 * Description:	Parse a postfix expression.
 *
 *		postfix-expression:
 *		  primary-expression
 *		  postfix-expression [ expression ]
 *		  postfix-expression . identifier
 *		  postfix-expression -> identifier
 */

static Expression *postfixExpression()
{
    Expression *left, *right;


    left = primaryExpression();

    while (1) {
	if (lookahead == '[') {
	    match('[');
	    right = expression();
	    left = checkArray(left, right);
	    match(']');

	} else if (lookahead == '.') {
	    match('.');
	    left = checkDirectField(left, expect(ID));

	} else if (lookahead == ARROW) {
	    match(ARROW);
	    left = checkIndirectField(left, expect(ID));

	} else
	    break;
    }

    return left;
}


/*
 * Function:	prefixExpression
 *
 * Description:	Parse a prefix expression.
 *
 *		prefix-expression:
 *		  postfix-expression
 *		  ! prefix-expression
 *		  - prefix-expression
 *		  * prefix-expression
 *		  & prefix-expression
 *		  sizeof prefix-expression
 *		  sizeof ( specifier pointers )
 */

static Expression *prefixExpression()
{
    Expression *expr;
    unsigned indirection;
    string typespec;


    if (lookahead == '!') {
	match('!');
	expr = prefixExpression();
	expr = checkNot(expr);

    } else if (lookahead == '-') {
	match('-');
	expr = prefixExpression();
	expr = checkNegate(expr);

    } else if (lookahead == '*') {
	match('*');
	expr = prefixExpression();
	expr = checkDereference(expr);

    } else if (lookahead == '&') {
	match('&');
	expr = prefixExpression();
	expr = checkAddress(expr);

    } else if (lookahead == SIZEOF) {
	match(SIZEOF);

	if (lookahead == '(' && isSpecifier(peek())) {
	    match('(');
	    typespec = specifier();
	    indirection = pointers();
	    match(')');
	    expr = checkSizeof(Type(typespec, indirection));

	} else {
	    expr = prefixExpression();
	    expr = checkSizeof(expr->type());
	}

    } else
	expr = postfixExpression();

    return expr;
}


/*
 * Function:	castExpression
 *
 * Description:	Parse a cast expression.  If the token after the opening
 *		parenthesis is not a type specifier, we could have a
 *		parenthesized expression instead.
 *
 *		cast-expression:
 *		  prefix-expression
 *		  ( specifier pointers ) cast-expression
 */

static Expression *castExpression()
{
    Expression *expr;
    unsigned indirection;
    string typespec;


    if (lookahead == '(' && isSpecifier(peek())) {
	match('(');
	typespec = specifier();
	indirection = pointers();
	match(')');
	expr = castExpression();
	expr = checkCast(Type(typespec, indirection), expr);

    } else
	expr = prefixExpression();

    return expr;
}


/*
 * Function:	multiplicativeExpression
 *
 * Description:	Parse a multiplicative expression.
 *
 *		multiplicative-expression:
 *		  cast-expression
 *		  multiplicative-expression * cast-expression
 *		  multiplicative-expression / cast-expression
 *		  multiplicative-expression % cast-expression
 */

static Expression *multiplicativeExpression()
{
    Expression *left, *right;


    left = castExpression();

    while (1) {
	if (lookahead == '*') {
	    match('*');
	    right = castExpression();
	    left = checkMultiply(left, right);

	} else if (lookahead == '/') {
	    match('/');
	    right = castExpression();
	    left = checkDivide(left, right);

	} else if (lookahead == '%') {
	    match('%');
	    right = castExpression();
	    left = checkRemainder(left, right);

	} else
	    break;
    }

    return left;
}


/*
 * Function:	additiveExpression
 *
 * Description:	Parse an additive expression.
 *
 *		additive-expression:
 *		  multiplicative-expression
 *		  additive-expression + multiplicative-expression
 *		  additive-expression - multiplicative-expression
 */

static Expression *additiveExpression()
{
    Expression *left, *right;


    left = multiplicativeExpression();

    while (1) {
	if (lookahead == '+') {
	    match('+');
	    right = multiplicativeExpression();
	    left = checkAdd(left, right);

	} else if (lookahead == '-') {
	    match('-');
	    right = multiplicativeExpression();
	    left = checkSubtract(left, right);

	} else
	    break;
    }

    return left;
}


/*
 * Function:	relationalExpression
 *
 * Description:	Parse a relational expression.  Note that Simple C does not
 *		have shift operators, so we go immediately to additive
 *		expressions.
 *
 *		relational-expression:
 *		  additive-expression
 *		  relational-expression < additive-expression
 *		  relational-expression > additive-expression
 *		  relational-expression <= additive-expression
 *		  relational-expression >= additive-expression
 */

static Expression *relationalExpression()
{
    Expression *left, *right;


    left = additiveExpression();

    while (1) {
	if (lookahead == '<') {
	    match('<');
	    right = additiveExpression();
	    left = checkLessThan(left, right);

	} else if (lookahead == '>') {
	    match('>');
	    right = additiveExpression();
	    left = checkGreaterThan(left, right);

	} else if (lookahead == LEQ) {
	    match(LEQ);
	    right = additiveExpression();
	    left = checkLessOrEqual(left, right);

	} else if (lookahead == GEQ) {
	    match(GEQ);
	    right = additiveExpression();
	    left = checkGreaterOrEqual(left, right);

	} else
	    break;
    }

    return left;
}


/*
 * Function:	equalityExpression
 *
 * Description:	Parse an equality expression.
 *
 *		equality-expression:
 *		  relational-expression
 *		  equality-expression == relational-expression
 *		  equality-expression != relational-expression
 */

static Expression *equalityExpression()
{
    Expression *left, *right;


    left = relationalExpression();

    while (1) {
	if (lookahead == EQL) {
	    match(EQL);
	    right = relationalExpression();
	    left = checkEqual(left, right);

	} else if (lookahead == NEQ) {
	    match(NEQ);
	    right = relationalExpression();
	    left = checkNotEqual(left, right);

	} else
	    break;
    }

    return left;
}


/*
 * Function:	logicalAndExpression
 *
 * Description:	Parse a logical-and expression.  Note that Simple C does
 *		not have bitwise-and expressions.
 *
 *		logical-and-expression:
 *		  equality-expression
 *		  logical-and-expression && equality-expression
 */

static Expression *logicalAndExpression()
{
    Expression *left, *right;


    left = equalityExpression();

    while (lookahead == AND) {
	match(AND);
	right = equalityExpression();
	left = checkLogicalAnd(left, right);
    }

    return left;
}


/*
 * Function:	expression
 *
 * Description:	Parse an expression, or more specifically, a logical-or
 *		expression, since Simple C does not allow comma or
 *		assignment as an expression operator.
 *
 *		expression:
 *		  logical-and-expression
 *		  expression || logical-and-expression
 */

static Expression *expression()
{
    Expression *left, *right;


    left = logicalAndExpression();

    while (lookahead == OR) {
	match(OR);
	right = logicalAndExpression();
	left = checkLogicalOr(left, right);
    }

    return left;
}


/*
 * Function:	statements
 *
 * Description:	Parse a possibly empty sequence of statements.  Rather than
 *		checking if the next token starts a statement, we check if
 *		the next token ends the sequence, since a sequence of
 *		statements is always terminated by a closing brace.
 *
 *		statements:
 *		  empty
 *		  statement statements
 */

static Statements statements()
{
    Statements stmts;


    while (lookahead != '}')
	stmts.push_back(statement());

    return stmts;
}


/*
 * Function:	statement
 *
 * Description:	Parse a statement.  Note that Simple C has so few
 *		statements that we handle them all in this one function.
 *
 *		statement:
 *		  { declarations statements }
 *		  return expression ;
 *		  while ( expression ) statement
 *		  if ( expression ) statement
 *		  if ( expression ) statement else statement
 *		  expression = expression ;
 *		  expression ;
 */

static Statement *statement()
{
    Scope *decls;
    Statement *stmt;
    Statements stmts;
    Expression *expr;


    if (lookahead == '{') {
	match('{');
	openScope();
	declarations();
	stmts = statements();
	decls = closeScope();
	match('}');
	return new Block(decls, stmts);
    }

    if (lookahead == RETURN) {
	match(RETURN);
	expr = expression();
	checkReturn(expr, returnType);
	match(';');
	return new Return(expr);
    }

    if (lookahead == WHILE) {
	match(WHILE);
	match('(');
	expr = expression();
	checkTest(expr);
	match(')');
	stmt = statement();
	return new While(expr, stmt);
    }

    if (lookahead == IF) {
	match(IF);
	match('(');
	expr = expression();
	checkTest(expr);
	match(')');
	stmt = statement();

	if (lookahead != ELSE)
	    return new If(expr, stmt, nullptr);

	match(ELSE);
	return new If(expr, stmt, statement());
    }

    expr = expression();

    if (lookahead == '=') {
	match('=');
	stmt = checkAssignment(expr, expression());

    } else
	stmt = expr;

    match(';');
    return stmt;
}


/*
 * Function:	parameter
 *
 * Description:	Parse a parameter, which in Simple C is always a scalar
 *		variable with optional pointer declarators.
 *
 *		parameter:
 *		  specifier pointers identifier
 */

static Type parameter()
{
    string typespec, name;
    unsigned indirection;


    typespec = specifier();
    indirection = pointers();
    name = expect(ID);

    Type type = Type(typespec, indirection);
    return declareParameter(name, type)->type();
}


/*
 * Function:	parameters
 *
 * Description:	Parse the parameters of a function, but not the opening or
 *		closing parentheses.
 *
 *		parameters:
 *		  void
 *		  parameter-list
 *
 *		parameter-list:
 *		  parameter
 *		  parameter , parameter-list
 */

static Parameters *parameters()
{
    Parameters *params = new Parameters();


    if (lookahead == VOID)
	match(VOID);

    else {
	params->push_back(parameter());

	while (lookahead == ',') {
	    match(',');
	    params->push_back(parameter());
	}
    }

    return params;
}


/*
 * Function:	topLevelDeclaration
 *
 * Description:	Parse a top level declaration, which is either a structure
 *		type definition, function definition, or global variable
 *		declaration.
 *
 *		type-definition:
 *		  struct identifier { declaration declarations } ;
 *
 *		global-declaration:
 *		  specifier global-declarator-list ;
 *
 *		global-declarator-list:
 *		  global-declarator
 *		  global-declarator , global-declarator-list
 *
 *		global-declarator:
 *		  pointers identifier
 *		  pointers identifier ( )
 *		  pointers identifier [ num ]
 *
 *		function-definition:
 *		  specifier pointers identifier ( parameters ) { ... }
 */

static void topLevelDeclaration()
{
    string typespec, name;
    unsigned indirection;
    Function *function;
    Statements stmts;
    Symbol *symbol;
    Scope *decls;


    typespec = specifier();

    if (typespec != "int" && typespec != "char" && lookahead == '{') {
	match('{');
	openScope();
	declaration();
	declarations();
	defineStructure(typespec, closeScope());
	match('}');
	match(';');

    } else {
	indirection = pointers();
	name = expect(ID);

	if (lookahead == '[') {
	    match('[');
	    symbol = declareVariable(name, Type(typespec, indirection, number()));
	    globals.push_back(symbol);
	    match(']');

	} else if (lookahead == '(') {
	    match('(');

	    if (lookahead == ')') {
		match(')');
		declareFunction(name, Type(typespec, indirection, nullptr));

	    } else {
		openScope();
		returnType = Type(typespec, indirection);
		symbol = defineFunction(name, Type(typespec, indirection, parameters()));
		match(')');
		match('{');
		declarations();
		stmts = statements();
		decls = closeScope();
		function = new Function(symbol, new Block(decls, stmts));
		match('}');

		if (numerrors == 0)
		    function->generate();

		return;
	    }

	} else {
	    symbol = declareVariable(name, Type(typespec, indirection));
	    globals.push_back(symbol);
	}

	while (lookahead == ',') {
	    match(',');
	    indirection = pointers();
	    name = expect(ID);

	    if (lookahead == '[') {
		match('[');
		symbol = declareVariable(name, Type(typespec, indirection, number()));
		globals.push_back(symbol);
		match(']');

	    } else if (lookahead == '(') {
		match('(');
		match(')');
		declareFunction(name, Type(typespec, indirection, nullptr));

	    } else {
		symbol = declareVariable(name, Type(typespec, indirection));
		globals.push_back(symbol);
	    }
	}

	match(';');
    }
}


/*
 * Function:	main
 *
 * Description:	Analyze the standard input stream.
 */

int main()
{
    openScope();
    lookahead = lexan(lexbuf);

    while (lookahead != DONE)
	topLevelDeclaration();

    if (numerrors == 0)
	generateGlobals(globals);

    closeScope();
    exit(EXIT_SUCCESS);
}
