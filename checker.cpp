/*
 * File:	checker.cpp
 *
 * Description:	This file contains the public and private function and
 *		variable definitions for the semantic checker for Simple C.
 *
 *		Extra functionality:
 *		- inserting an undeclared symbol with the error type
 */

# include <map>
# include <cassert>
# include <iostream>
# include "lexer.h"
# include "checker.h"
# include "nullptr.h"
# include "tokens.h"
# include "Symbol.h"
# include "Scope.h"
# include "Type.h"


using namespace std;

static map<string,Scope *> fields;
static Scope *outermost, *toplevel;
static const Type error, integer("int"), character("char");

static string undeclared = "'%s' undeclared";
static string redefined = "redefinition of '%s'";
static string redeclared = "redeclaration of '%s'";
static string conflicting = "conflicting types for '%s'";
static string incomplete = "incomplete type for '%s'";
static string nonpointer = "pointer type required for '%s'";

static string invalid_return = "invalid return type";
static string invalid_test = "invalid type for test expression";
static string invalid_lvalue = "lvalue required in expression";
static string invalid_operand = "invalid operand to unary %s";
static string invalid_operands = "invalid operands to binary %s";
static string invalid_cast = "invalid operand in cast expression";
static string invalid_sizeof = "invalid operand in sizeof expression";
static string invalid_function = "called object is not a function";
static string invalid_arguments = "invalid arguments to called function";
static string incomplete_type = "using pointer to incomplete type";


/*
 * Function:	debug
 */

static void debug(const string &str, const Type &t1, const Type &t2)
{
    // cout << "line " << lineno << ": " << str << " " << t1 << " to " << t2 << endl;
}


/*
 * Function:	promote
 *
 * Description:	Perform type promotion on the given expression.  An array
 *		is promoted to a pointer by explicitly inserting an address
 *		operator.  A character is promoted to an integer by
 *		explicitly inserting a type cast.
 */

static Type promote(Expression *&expr)
{
    if (expr->type().isArray()) {
	debug("promoting", expr->type(), expr->type().promote());
	expr = new Address(expr, expr->type().promote());

    } else if (expr->type() == character) {
	debug("promoting", character, integer);
	expr = new Cast(integer, expr);
    }

    return expr->type();
}


/*
 * Function:	isIncomplete
 *
 * Description:	Check if the specified type is incomplete.
 */

static bool isIncomplete(const Type &t)
{
    return !t.isPointer() && t.isStruct() && fields.count(t.specifier()) == 0;
}


/*
 * Function:	isIncompletePointer
 *
 * Description:	Check if the specified type is a pointer to an incomplete
 *		type.
 */

static bool isIncompletePointer(const Type &t)
{
    return t.isPointer() && isIncomplete(t.deref());
}


/*
 * Function:	checkIfComplete
 *
 * Description:	Check if the given type is complete.  A non-structure type
 *		is always complete.  A structure type is complete if its
 *		fields have been defined.
 */

static Type checkIfComplete(const string &name, const Type &type)
{
    if (!type.isStruct() || type.indirection() > 0)
	return type;

    if (fields.count(type.specifier()) > 0)
	return type;

    report(incomplete, name);
    return error;
}


/*
 * Function:	checkIfStructure
 *
 * Description:	Check if the given type is a structure.
 */

static Type checkIfStructure(const string &name, const Type &type)
{
    if (!type.isStruct() || type.indirection() > 0)
	return type;

    report(nonpointer, name);
    return error;
}


/*
 * Function:	openScope
 *
 * Description:	Create a scope and make it the new top-level scope.
 */

Scope *openScope()
{
    toplevel = new Scope(toplevel);

    if (outermost == nullptr)
	outermost = toplevel;

    return toplevel;
}


/*
 * Function:	closeScope
 *
 * Description:	Remove the top-level scope, and make its enclosing scope
 *		the new top-level scope.
 */

Scope *closeScope()
{
    Scope *old = toplevel;

    toplevel = toplevel->enclosing();
    return old;
}


/*
 * Function:	getFields
 *
 * Description:	Return the fields associated with the specified structure.
 */

Symbols getFields(const string &name)
{
    assert(fields.count(name) > 0);
    return fields[name]->symbols();
}


/*
 * Function:	defineStructure
 *
 * Description:	Define a structure with the specified NAME and whose fields
 *		are specified by SCOPE.  A structure can be defined only
 *		once.
 */

void defineStructure(const string &name, Scope *scope)
{
    if (fields.count(name) > 0) {
	report(redefined, name);
	delete scope;
    } else
	{
		fields[name] = scope;
		Type t(name);
		t.size();
	}
}


/*
 * Function:	defineFunction
 *
 * Description:	Define a function with the specified NAME and TYPE.  A
 *		function is always defined in the outermost scope.  This
 *		definition always replaces any previous definition or
 *		declaration.
 */

Symbol *defineFunction(const string &name, const Type &type)
{
    Symbol *symbol = outermost->find(name);

    if (symbol != nullptr) {
	if (symbol->type().isFunction() && symbol->type().parameters()) {
	    report(redefined, name);
	    delete symbol->type().parameters();

	} else if (type != symbol->type())
	    report(conflicting, name);

	outermost->remove(name);
	delete symbol;
    }

    symbol = new Symbol(name, checkIfStructure(name, type));
    outermost->insert(symbol);

    return symbol;
}


/*
 * Function:	declareFunction
 *
 * Description:	Declare a function with the specified NAME and TYPE.  A
 *		function is always declared in the outermost scope.  Any
 *		redeclaration is discarded.
 */

Symbol *declareFunction(const string &name, const Type &type)
{
    Symbol *symbol = outermost->find(name);

    if (symbol == nullptr) {
	symbol = new Symbol(name, checkIfStructure(name, type));
	outermost->insert(symbol);

    } else if (type != symbol->type()) {
	report(conflicting, name);
	delete type.parameters();
    }

    return symbol;
}


/*
 * Function:	declareParameter
 *
 * Description:	Declare a parameter with the specified NAME and TYPE.  Any
 *		redeclaration is discarded.  The only difference between
 *		declaring a parameter and a variable is that a parameter
 *		cannot be a structure type.
 */

Symbol *declareParameter(const string &name, const Type &type)
{
    return declareVariable(name, checkIfStructure(name, type));
}


/*
 * Function:	declareVariable
 *
 * Description:	Declare a variable with the specified NAME and TYPE.  Any
 *		redeclaration is discarded.
 */

Symbol *declareVariable(const string &name, const Type &type)
{
    Symbol *symbol = toplevel->find(name);

    if (symbol == nullptr) {
	symbol = new Symbol(name, checkIfComplete(name, type));
	toplevel->insert(symbol);

    } else if (outermost != toplevel)
	report(redeclared, name);

    else if (type != symbol->type())
	report(conflicting, name);

    return symbol;
}


/*
 * Function:	checkIdentifier
 *
 * Description:	Check if NAME is declared.  If it is undeclared, then
 *		declare it as having the error type in order to eliminate
 *		future error messages.
 */

Symbol *checkIdentifier(const string &name)
{
    Symbol *symbol = toplevel->lookup(name);

    if (symbol == nullptr) {
	report(undeclared, name);
	symbol = new Symbol(name, error);
	toplevel->insert(symbol);
    }

    return symbol;
}


/*
 * Function:	checkCall
 *
 * Description:	Check a function call expression: the type of the object
 *		being called must be a function type, and the number and
 *		types of arguments must agree.
 */

Expression *checkCall(Symbol *id, Expressions &args)
{
    const Type &t = id->type();
    Type arg, result = error;


    if (t != error) {
	if (!t.isFunction())
	    report(invalid_function);

    	else {
	    Parameters *params = t.parameters();
	    result = Type(t.specifier(), t.indirection());

	    for (unsigned i = 0; i < args.size(); i ++) {
		arg = promote(args[i]);

		if (arg != error && !arg.isSimple()) {
		    report(invalid_arguments);
		    result = error;
		    break;
		}
	    }

	    if (params != nullptr && result != error) {
		if (params->size() != args.size())
		    report(invalid_arguments);

		else {
		    for (unsigned i = 0; i < args.size(); i ++)
			if (!(*params)[i].isCompatibleWith(args[i]->type())) {
			    report(invalid_arguments);
			    result = error;
			    break;
			}
		}

	    }
	}
    }

    return new Call(id, args, result);
}


/*
 * Function:	checkArray
 *
 * Description:	Check an array index expression: the left operand must have
 *		type "pointer to T" and the right operand must have type
 *		int, and the result has type T.
 */

Expression *checkArray(Expression *left, Expression *right)
{
    const Type &t1 = promote(left);
    const Type &t2 = promote(right);
    Type result = error;

    if (t1.isPointer() && t1.deref().size() > 1)
	right = new Multiply(right, new Number(t1.deref().size()), integer);

    Expression *expr = new Add(left, right, t1);

    if (t1 != error && t2 != error) {
	if (isIncompletePointer(t1))
	    report(incomplete_type);
	else if (t1.isPointer() && t2 == integer)
	    result = t1.deref();
	else
	    report(invalid_operands, "[]");
    }

    return new Dereference(expr, result);
}


/*
 * Function:	checkDirectField
 *
 * Description:	Check a direct structure field reference expression: the
 *		expression must have a structure type, the identifier must
 *		be a field of that structure, and the result has the type
 *		of the field.  If the identifier is not a member of the
 *		structure, we go ahead and declare it and give it the error
 *		type, so we only get the error once.
 */

Expression *checkDirectField(Expression *expr, const string &id)
{
    Scope *scope;
    Symbol *symbol = nullptr;
    const Type &t = expr->type();
    Type result = error;


    if (t != error) {
	if (!t.isStruct() || t.indirection() > 0)
	    report(invalid_operands, ".");

	else {
	    if (isIncomplete(t))
		report(incomplete_type);

	    else {
		scope = fields[t.specifier()];
		symbol = scope->find(id);
		t.size();

		if (symbol == nullptr) {
		    report(invalid_operands, ".");
		    symbol = new Symbol(id, error);
		    scope->insert(symbol);
		}

		result = symbol->type();
	    }
	}
    }

    if (symbol == nullptr)
	symbol = new Symbol("-unknown-", error);

    return new Field(expr, new Identifier(symbol), result);
}


/*
 * Function:	checkIndirectField
 *
 * Description:	Check an indirect field reference expression: the
 *		expression must have type pointer(T), where T is a
 *		structure type, the identifier must be a field of that
 *		structure, and the result has the type of the field.
 */

Expression *checkIndirectField(Expression *expr, const string &id)
{
    Scope *scope;
    Symbol *symbol = nullptr;
    Type t = expr->type().promote();
    Type result = error;


    if (t != error) {
	if (!t.isStruct() || t.indirection() != 1)
	    report(invalid_operands, "->");

	else {
	    if (isIncompletePointer(t))
		report(incomplete_type);

	    else {
		scope = fields[t.specifier()];
		symbol = scope->find(id);
		t = t.deref();

		if (symbol == nullptr) {
		    report(invalid_operands, "->");
		    symbol = new Symbol(id, error);
		    scope->insert(symbol);
		}

		result = symbol->type();
	    }
	}
    }

    if (symbol == nullptr)
	symbol = new Symbol("-unknown-", error);

    return new Field(new Dereference(expr, t), new Identifier(symbol), result);
}
 

/*
 * Function:	checkNot
 *
 * Description:	Check a logical negation expression: the operand must have a
 *		simple type, and the result has type int.
 */

Expression *checkNot(Expression *expr)
{
    const Type &t = promote(expr);
    Type result = error;


    if (t != error) {
	if (t.isSimple())
	    result = integer;
	else
	    report(invalid_operand, "!");
    }

    return new Not(expr, result);
}


/*
 * Function:	checkNegate
 *
 * Description:	Check an arithmetic negation expression: the operand must
 *		have type int, and the result has type int.
 */

Expression *checkNegate(Expression *expr)
{
    const Type &t = promote(expr);
    Type result = error;


    if (t != error) {
	if (t.isInteger())
	    result = t;
	else
	    report(invalid_operand, "-");
    }

    return new Negate(expr, result);
}


/*
 * Function:	checkDereference
 *
 * Description:	Check a dereference expression: the operand must have type
 *		"pointer to T," and the result has type T.
 */

Expression *checkDereference(Expression *expr)
{
    const Type &t = promote(expr);
    Type result = error;


    if (t != error) {
	if (isIncompletePointer(t))
	    report(incomplete_type);
	else if (t.isPointer())
	    result = t.deref();
	else
	    report(invalid_operand, "*");
    }

    return new Dereference(expr, result);
}


/*
 * Function:	checkAddress
 *
 * Description:	Check an address expression: the operand must be an lvalue,
 *		and if the operand has type T, then the result has type
 *		"pointer to (T)."
 */

Expression *checkAddress(Expression *expr)
{
    const Type &t = expr->type();
    Type result = error;


    if (t != error) {
	if (expr->lvalue())
	    result = Type(t.specifier(), t.indirection() + 1);
	else
	    report(invalid_lvalue);
    }

    return new Address(expr, result);
}


/*
 * Function:	checkSizeof
 *
 * Description:	Check a sizeof expression: the type cannot be a function
 *		type and must be complete.
 */

Expression *checkSizeof(const Type &type)
{
    if (type != error)
	if (type.isFunction() || isIncomplete(type))
	    report(invalid_sizeof);

    return new Number(type.size());
}


/*
 * Function:	checkCast
 *
 * Description:	Check a cast expression: the result type and type of the
 *		operand must both be simple types.
 */

Expression *checkCast(const Type &type, Expression *expr)
{
    const Type &t = promote(expr);
    Type result = error;


    if (t != error) {
	if (type.isSimple() && t.isSimple())
	    result = type;
	else
	    report(invalid_cast);
    }

    return new Cast(result, expr);
}


/*
 * Function:	checkMult
 *
 * Description:	Check a multiplicative expression: both operands must have
 *		a type int, and the result has type int.
 */

static Type checkMult(Expression *&left, Expression *&right, const string &op)
{
    const Type &t1 = promote(left);
    const Type &t2 = promote(right);
    Type result = error;


    if (t1 != error && t2 != error) {
	if (t1.isInteger() && t2.isInteger())
	    result = t1;
	else
	    report(invalid_operands, op);
    }

    return result;
}


/*
 * Function:	checkMultiply
 *
 * Description:	Check a multiplication expression.
 */

Expression *checkMultiply(Expression *left, Expression *right)
{
    Type t = checkMult(left, right, "*");
    return new Multiply(left, right, t);
}


/*
 * Function:	checkDivide
 *
 * Description:	Check a division expression.
 */

Expression *checkDivide(Expression *left, Expression *right)
{
    Type t = checkMult(left, right, "/");
    return new Divide(left, right, t);
}

/*
 * Function:	checkRemainder
 *
 * Description:	Check a remainder expression.
 */

Expression *checkRemainder(Expression *left, Expression *right)
{
    Type t = checkMult(left, right, "%");
    return new Remainder(left, right, t);
}


/*
 * Function:	checkAdd
 *
 * Description:	Check an addition expression: if both operands have type
 *		int, then the result has type int; if one operand has a
 *		pointer type and other operand has type int, the result has
 *		that pointer type.
 */

Expression *checkAdd(Expression *left, Expression *right)
{
    const Type &t1 = promote(left);
    const Type &t2 = promote(right);
    Type result = error;


    if (t1 != error && t2 != error) {
	if (isIncompletePointer(t1) || isIncompletePointer(t2))
	    report(incomplete_type);

	else if (t1.isInteger() && t2.isInteger())
	    result = t1;

	else if (t1.isPointer() && t2 == integer) {
	    if (t1.deref().size() > 1)
		right = new Multiply(right, new Number(t1.deref().size()), integer);

	    result = t1;

	} else if (t1 == integer && t2.isPointer()) {
	    if (t2.deref().size() > 1)
		left = new Multiply(left, new Number(t2.deref().size()), integer);

	    result = t2;

	} else
	    report(invalid_operands, "+");
    }

    return new Add(left, right, result);
}


/*
 * Function:	checkSubtract
 *
 * Description:	Check a subtraction expression: if both operands have
 *		type int, then the result has type int; if the left operand
 *		has a pointer type and the right operand has type int, then
 *		the result has that pointer type; if both operands are
 *		identical pointer types, then the result has type int.
 */

Expression *checkSubtract(Expression *left, Expression *right)
{
    Expression *tree;
    const Type &t1 = promote(left);
    const Type &t2 = promote(right);
    Type result = error;
    Type deref;


    if (t1 != error && t2 != error) {
	if (isIncompletePointer(t1) || isIncompletePointer(t2))
	    report(incomplete_type);

	else if (t1.isInteger() && t2.isInteger())
	    result = t1;

	else if (t1.isPointer() && t1 == t2)
	    result = integer;

	else if (t1.isPointer() && t2 == integer) {
	    if (t1.deref().size() > 1)
		right = new Multiply(right, new Number(t1.deref().size()), integer);

	    result = t1;

	} else
	    report(invalid_operands, "-");
    }

    tree = new Subtract(left, right, result);

    if (t1.isPointer() && t1 == t2 && t1.deref().size() > 1)
	tree = new Divide(tree, new Number(t1.deref().size()), integer);

    return tree;
}


/*
 * Function:	checkCompare
 *
 * Description:	Check an equality or relational expression: the types of
 *		both operands must be compatible, and the result has type
 *		int.
 */

static
Type checkCompare(Expression *&left, Expression *&right, const string &op)
{
    const Type &t1 = promote(left);
    const Type &t2 = promote(right);
    Type result = error;


    if (t1 != error && t2 != error) {
	if (t1.isCompatibleWith(t2))
	    result = integer;
	else
	    report(invalid_operands, op);
    }

    return result;
}


/*
 * Function:	checkLessThan
 *
 * Description:	Check a less-than expression: left < right.
 */

Expression *checkLessThan(Expression *left, Expression *right)
{
    Type t = checkCompare(left, right, "<");
    return new LessThan(left, right, t);
}


/*
 * Function:	checkGreaterThan
 *
 * Description:	Check a greater-than expression: left > right.
 */

Expression *checkGreaterThan(Expression *left, Expression *right)
{
    Type t = checkCompare(left, right, ">");
    return new GreaterThan(left, right, t);
}


/*
 * Function:	checkLessOrEqual
 *
 * Description:	Check a less-than-or-equal expression: left <= right.
 */

Expression *checkLessOrEqual(Expression *left, Expression *right)
{
    Type t = checkCompare(left, right, "<=");
    return new LessOrEqual(left, right, t);
}


/*
 * Function:	checkGreaterOrEqual
 *
 * Description:	Check a greater-than-or-equal expression: left >= right.
 */

Expression *checkGreaterOrEqual(Expression *left, Expression *right)
{
    Type t = checkCompare(left, right, ">=");
    return new GreaterOrEqual(left, right, t);
}


/*
 * Function:	checkEqual
 *
 * Description:	Check an equality expression: left == right.
 */

Expression *checkEqual(Expression *left, Expression *right)
{
    Type t = checkCompare(left, right, "==");
    return new Equal(left, right, t);
}


/*
 * Function:	checkNotEqual
 *
 * Description:	Check an inequality expression: left != right.
 */

Expression *checkNotEqual(Expression *left, Expression *right)
{
    Type t = checkCompare(left, right, "!=");
    return new NotEqual(left, right, t);
}


/*
 * Function:	checkLogical
 *
 * Description:	Check a logical-or or logical-and expression: the types of
 *		both operands must be simple types and the result has type
 *		int.
 */

static
Type checkLogical(Expression *&left, Expression *&right, const string &op)
{
    const Type &t1 = promote(left);
    const Type &t2 = promote(right);
    Type result = error;


    if (t1 != error && t2 != error) {
	if (t1.isSimple() && t2.isSimple())
	    result = integer;
	else
	    report(invalid_operands, op);
    }

    return result;
}


/*
 * Function:	checkLogicalAnd
 *
 * Description:	Check a logical-and expression: left && right.
 */

Expression *checkLogicalAnd(Expression *left, Expression *right)
{
    Type t = checkLogical(left, right, "&&");
    return new LogicalAnd(left, right, t);
}


/*
 * Function:	checkLogicalOr
 *
 * Description:	Check a logical-or expression: left || right.
 */

Expression *checkLogicalOr(Expression *left, Expression *right)
{
    Type t = checkLogical(left, right, "||");
    return new LogicalOr(left, right, t);
}


/*
 * Function:	checkAssignment
 *
 * Description:	Check an assignment statement: the left operand must be an
 *		lvalue and the type of the operands must be compatible.
 */

Statement *checkAssignment(Expression *left, Expression *right)
{
    const Type &t1 = left->type();
    const Type &t2 = promote(right);


    if (t1 != error && t2 != error) {
	if (!left->lvalue())
	    report(invalid_lvalue);

	else if (!t1.isCompatibleWith(t2))
	    report(invalid_operands, "=");
    }

    return new Assignment(left, right);
}


/*
 * Function:	checkReturn
 *
 * Description:	Check a return statement: the type of the expression must
 *		be compatible with the given type, which should be the
 *		return type of the enclosing function.
 */

void checkReturn(Expression *&expr, const Type &type)
{
    const Type &t = promote(expr);

    if (t != error && !t.isCompatibleWith(type))
	report(invalid_return);
}


/*
 * Function:	checkTest
 *
 * Description:	Check if the type of the expression is a legal type in a
 *		test expression in a while, if-then, or if-then-else
 *		statement: the type must be a simple type.
 */

void checkTest(Expression *&expr)
{
    const Type &t = promote(expr);

    if (t != error && !t.isSimple())
	report(invalid_test);
}
