/*
 * File:	generator.cpp
 *
 * Description:	This file contains the public and member function
 *		definitions for the code generator for Simple C.
 *
 *		Extra functionality:
 *		- putting all the global declarations at the end
 */

# include <sstream>
# include <iostream>
# include "generator.h"
# include "machine.h"
# include "lexer.h"

using namespace std;

static unsigned maxargs;

/*
 * Function: Expression
 *
 * These two functions will handle indirection
 */

void Expression::generate(bool &indirect)
{
	cerr << "Calling Bool Indirect" << endl;
	indirect = false;
	generate();
}

void Expression::generate()
{
	cerr << "Forgot Something?" << endl;

}


/*
 * Function: AssignTempOffset
 * 
 * This will increase the offset so that there is space for a temp variable
 *
 */

int offset;
void assignTempOffset(Expression *expr)
{
	stringstream ss;
	offset -= expr->type().size();
	ss << offset << "(%ebp)";
	expr -> _operand = ss.str();
}

/*
 * Struct (default public class) + other following functions: Labels
 *
 * Description: This will be used for labelling jumps to be used in Control Flow
 *
 */

struct Label{
	static unsigned int counter;
	int number;
	Label();
};

// Set counter to zero
unsigned Label::counter = 0;

//Label Constructor
Label::Label()
{
	number = counter++;
}

//Overload ostream operator
ostream &operator<<(ostream &ostr, const Label &label)
{
	ostr << ".L" << label.number;
	return ostr;
}

//Global Return Label (needs to be reused and set for every function)
Label *returnLabel;

/*
 * Function:	operator <<
 *
 * Description:	Convenience function for writing the operand of an
 *		expression.
 */

ostream &operator <<(ostream &ostr, Expression *expr)
{
    return ostr << expr->_operand;
}


/*
 * Function:	Identifier::generate
 *
 * Description:	Generate code for an identifier.  Since there is really no
 *		code to generate, we simply update our operand.
 */

void Identifier::generate()
{
    stringstream ss;


    if (_symbol->_offset != 0)
	ss << _symbol->_offset << "(%ebp)";
    else
	ss << global_prefix << _symbol->name();

    _operand = ss.str();
}


/*
 * Function:	Number::generate
 *
 * Description:	Generate code for a number.  Since there is really no code
 *		to generate, we simply update our operand.
 */

void Number::generate()
{
    stringstream ss;

    ss << "$" << _value;
    _operand = ss.str();
}

/*
 * Function:	Character::generate
 *
 * Description:	Generate code for a character.  Since there is really no code
 *		to generate, we simply update our operand. Use charval() for character value.
 */

void Character::generate()
{

    stringstream ss;

    ss << "$" << charval(_value);
    _operand = ss.str();
}

# if STACK_ALIGNMENT == 4

/*
 * Function:	Call::generate
 *
 * Description:	Generate code for a function call expression, in which each
 *		argument is simply a variable or an integer literal.
 *
 *		**NOTE: NEED TO FIX FUNCTION TO HANDLE RECURSION**
 */

void Call::generate()
{
	cerr << "function called" << endl;
    unsigned numBytes = 0;


    for (int i = _args.size() - 1; i >= 0; i --) {
	_args[i]->generate();
	cout << "\tpushl\t" << _args[i] << endl;
	numBytes += _args[i]->type().size();
    }

    cout << "\tcall\t" << global_prefix << _id->name() << endl;

    if (numBytes > 0)
	cout << "\taddl\t$" << numBytes << ", %esp" << endl;
	
	//store this into register %eax
	//cout << "\tmovl\t" << this << ", %eax"  << endl;
	//store register %eax into this
	assignTempOffset(this);
	cout << "\tmovl\t%eax, " << this  << endl;

}

# else

/*
 * If the stack has to be aligned to a certain size before a function call
 * then we cannot push the arguments in the order we see them.  If we had
 * nested function calls, we cannot guarantee that the stack would be
 * aligned.
 *
 * Instead, we must know the maximum number of arguments so we can compute
 * the size of the frame.  Again, we cannot just move the arguments onto
 * the stack as we see them because of nested function calls.  Rather, we
 * have to generate code for all arguments first and then move the results
 * onto the stack.  This will likely cause a lot of spills.
 *
 * For now, since each argument is going to be either a number of in
 * memory, we just load it into %eax and then move %eax onto the stack.
 */

void Call::generate()
{
	cerr << "function called" << endl;
    if (_args.size() > maxargs)
	maxargs = _args.size();

    for (int i = _args.size() - 1; i >= 0; i --) {
	_args[i]->generate();
	cout << "\tmovl\t" << _args[i] << ", %eax" << endl;
	cout << "\tmovl\t%eax, " << i * SIZEOF_ARG << "(%esp)" << endl;
    }

    cout << "\tcall\t" << global_prefix << _id->name() << endl;
	assignTempOffset(this);
	cout << "\tmovl\t%eax, " << this  << endl;

}

# endif


/*
 * Function:	Assignment::generate
 *
 * Description:	Generate code for this assignment statement, in which the
 *		right-hand side is an integer literal and the left-hand
 *		side is an integer scalar variable.  Actually, the way
 *		we've written things, the right-side can be a variable too.
 *
 *		As of phase 6, checks for lvalues and size
 */

void Assignment::generate()
{
	//Indirect?
	bool indirect = false;
	//Do other generations
    _left->generate(indirect);
	cerr << "left part of assignment: " << _left << endl;
    _right->generate();
	cerr << "right part of assignment: " << _right << endl;

	//Assign to either char or int
    if(!indirect)
	{
		//Assign to char
		if(_left->type().size() == 1)
		{
			//load
			cout << "\tmovl\t" << _right << ", %eax" << endl;
			//store
			cout << "\tmovb\t%al, " << _left << endl;
		}
		//Assign to int
		if(_left->type().size() == 4)
		{
			//load
			cout << "\tmovl\t" << _right << ", %eax" << endl;
			//store
			cout << "\tmovl\t%eax, " << _left << endl;
		}
	}
	//Assign to either char* or int*
	else
	{
		//Assign to char*
		if(_left->type().size() == 1)
		{
			//load
			cout << "\tmovl\t" << _right << ", %eax" << endl;
		
			//op
			cout << "\tmovl\t" << _left << ", %ecx" << endl;
		
			//store
			cout << "\tmovb\t%al, (%ecx) " << endl;
		}
		//Assign to int*
		if(_left->type().size() == 4)
		{
			//load
			cout << "\tmovl\t" << _right << ", %eax" << endl;

			//op
			cout << "\tmovl\t" << _left << ", %ecx" << endl;

			//store
			cout << "\tmovl\t%eax, (%ecx)" << endl;
		}
	}
}


/*
 * Function:	Block::generate
 *
 * Description:	Generate code for this block, which simply means we
 *		generate code for each statement within the block.
 */

void Block::generate()
{
    for (unsigned i = 0; i < _stmts.size(); i ++)
	_stmts[i]->generate();
}


/*
 * Function:	Function::generate
 *
 * Description:	Generate code for this function, which entails allocating
 *		space for local variables, then emitting our prologue, the
 *		body of the function, and the epilogue.
 */

void Function::generate()
{
    offset = 0;
	returnLabel = new Label();

    /* Generate our prologue. */

    allocate(offset);
    cout << global_prefix << _id->name() << ":" << endl;
    cout << "\tpushl\t%ebp" << endl;
    cout << "\tmovl\t%esp, %ebp" << endl;
    cout << "\tsubl\t$" << _id->name() << ".size, %esp" << endl;


    /* Generate the body of this function. */

	maxargs = 0;
	_body->generate();

	offset -= maxargs * SIZEOF_ARG;

	while ((offset - PARAM_OFFSET) % STACK_ALIGNMENT)
		offset --;


	/* Generate our epilogue. */

	//Generate new label for return
	cout << *returnLabel << ":" << endl;

	cout << "\tmovl\t%ebp, %esp" << endl;
	cout << "\tpopl\t%ebp" << endl;
	cout << "\tret" << endl << endl;

	cout << "\t.globl\t" << global_prefix << _id->name() << endl;
	cout << "\t.set\t" << _id->name() << ".size, " << -offset << endl;

	cout << endl;
}


/*
 * Function:	generateGlobals
 *
 * Description:	Generate code for any global variable declarations.
 */

void generateGlobals(const Symbols &globals)
{
	if (globals.size() > 0)
		cout << "\t.data" << endl;

	for (unsigned i = 0; i < globals.size(); i ++) {
		cout << "\t.comm\t" << global_prefix << globals[i]->name();
		cout << ", " << globals[i]->type().size();
		cout << ", " << globals[i]->type().alignment() << endl;
	}
}

/*
 * Function: Add::generate
 * 
 * Description: Generate "addition" ASM code
 *
 */

void Add::generate()
{
	//Do other generations first
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load Op Store
	cout << "\tmovl\t" << _left << ", %eax" << endl;
	cout << "\taddl\t" << _right << ", %eax" << endl;
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: Subtract::generate
 *
 * Description: Generate "subtraction" ASM code
 *
 */

void Subtract::generate()
{
	//Do other generations first
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load Op Store
	cout << "\tmovl\t" << _left << ", %eax" << endl;
	cout << "\tsubl\t" << _right << ", %eax" << endl;
	cout << "\tmovl\t%eax, " << this << endl;
}
/*
 * Function: Multiply::generate
 *
 * Description: Generate "multiplication" ASM code
 *
 */

void Multiply::generate()
{
	//Do other generations first
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load Op Store
	cout << "\tmovl\t" << _left << ", %eax" << endl;
	cout << "\timull\t" << _right << ", %eax" << endl;
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: Divide::generate
 *
 * Description: Generate "division" ASM code
 *
 */

void Divide::generate()
{
	//Do other generations first
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;
	cout << "\tmovl\t" << _right << ", %ecx" << endl;

	//Op
	cout << "\tcltd\t" << endl;
	cout << "\tidivl\t%ecx " << endl;

	//Store (%eax contains result)
	cout << "\tmovl\t%eax, " << this << endl; 
}

/*
 * Function: Remainder::generate
 *
 * Description: Generate "modulo" or "remainder"  ASM code
 *
 */

void Remainder::generate()
{
	//Do other generations first
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;
	cout << "\tmovl\t" << _right << ", %ecx" << endl;

	//Op
	cout << "\tcltd\t" << endl;
	cout << "\tidivl\t%ecx " << endl;

	//Store (%ecx contains remainder)
	cout << "\tmovl\t%edx, " << this << endl; 
}

/*
 * Function: LessThan::generate
 *
 * Description: Generate "less than" ASM code
 */

void LessThan::generate()
{
	//Do other generations
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;

	//Start Op
	cout << "\tcmpl\t" << _right << ", %eax" << endl;

	//This is the only line that changes amongst the Comparison statments
	cout << "\tsetl %al" << endl;

	cout << "\tmovzbl %al, %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl %eax, " << this << endl;
}

/*
 * Function: GreaterThan::generate
 *
 * Description: Generate "greater than" ASM code
 */

void GreaterThan::generate()
{
	//Do other generations
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;

	//Start Op
	cout << "\tcmpl\t" << _right << ", %eax" << endl;

	//This is the only line that changes amongst the Comparison statments
	cout << "\tsetg %al" << endl;

	cout << "\tmovzbl %al, %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl %eax, " << this << endl;
}

/*
 * Function: LessOrEqual::generate
 *
 * Description: Generate "less than or equal to" ASM code
 */

void LessOrEqual::generate()
{
	//Do other generations
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;

	//Start Op
	cout << "\tcmpl\t" << _right << ", %eax" << endl;

	//This is the only line that changes amongst the Comparison statments
	cout << "\tsetle %al" << endl;

	cout << "\tmovzbl %al, %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl %eax, " << this << endl;
}

/*
 * Function: GreaterOrEqual::generate
 *
 * Description: Generate "greater than or equal to" ASM code
 */

void GreaterOrEqual::generate()
{
	//Do other generations
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;

	//Start Op
	cout << "\tcmpl\t" << _right << ", %eax" << endl;

	//This is the only line that changes amongst the Comparison statments
	cout << "\tsetge %al" << endl;

	cout << "\tmovzbl %al, %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl %eax, " << this << endl;
}

/*
 * Function: Equal::generate
 *
 * Description: Generate "is equal to" ASM code
 */

void Equal::generate()
{
	//Do other generations
	_left -> generate();
	_right -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;

	//Start Op
	cout << "\tcmpl\t" << _right << ", %eax" << endl;

	//This is the only line that changes amongst the Comparison statments
	cout << "\tsete\t%al" << endl;

	cout << "\tmovzbl\t%al, %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: NotEqual::generate
 *
 * Description: Generate "is not equal to" ASM code
 */

void NotEqual::generate()
{
	//Do other generations
	_left -> generate();
	_right -> generate();
	
	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _left << ", %eax" << endl;

	//Start Op
	cout << "\tcmpl\t" << _right << ", %eax" << endl;

		//This is the only line that changes amongst the Comparison statments
		cout << "\tsetne\t%al" << endl;

	cout << "\tmovzbl\t%al, %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: Not::generate
 *
 * Description: Generate "logical negation expression(!)" ASM code
 */

void Not::generate()
{
	//Do other generations
	_expr -> generate();
	
	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _expr << ", %eax" << endl;

	//Start Op
	cout << "\tcmpl\t $0" << ", %eax" << endl;
	cout << "\tsete\t%al" << endl;
	cout << "\tmovzbl\t%al, %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: Negate::generate
 *
 * Description: Generate "arithmetic negation expression (-)" ASM code
 */

void Negate::generate()
{
	//Do other generations
	_expr -> generate();
	
	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _expr << ", %eax" << endl;

	//Start Op
	cout << "\tnegl\t%eax" << endl;
	//End Op

	//Store
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: Cast::generate
 *
 * Description: Generate "cast" operator ASM code
 */
void Cast::generate()
{
	// .type() will return the new type
		Type src = _expr->type();
		Type dest = this->type();
	// _expr.type() will return the old type
	
	//Do other generations
	_expr -> generate();

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//If Dest size == 4 and Src Size == 1 
	if(dest.size() == 4 && src.size() == 1)
	{
		//Load Byte
		cout << "\tmovb\t" << _expr << ", %al" << endl;
		
		//Op
		cout << "\tmovsbl\t%al, %eax" << endl;

		//Store into Long
		cout << "\tmovl\t%eax, " << this << endl;
	}
	
	//If Dest Size == 1 and Src Size == 1 
	else if(dest.size() == 1 && src.size() == 4)
	{
		//Load Long
		cout << "\tmovl\t" << _expr << ", %eax" << endl;
		//Op
		//Do Nothing
		//Store into Byte
		cout << "\tmovb\t %al, " << this << endl;
	}
	//If Dest Size == 1 and Src Size == 1
	else if(dest.size() == 1 && src.size() == 1)
	{
		//Load Byte
		cout << "\tmovb\t" << _expr << ", %al" << endl;
		//Op
		//Do Nothing
		//Store into Byte
		cout << "\tmovb\t%al, " << this << endl;
	}
	//If Dest Size == 4 and Src Size == 4
	else if(dest.size() == 4 && src.size() == 4)
	{
		//Load Long
		cout << "\tmovl\t" << _expr << ", %eax" << endl;
		//Op
		//Do Nothing
		//Store into Long
		cout << "\tmovl\t%eax, " << this << endl;
	}
	else
	{
		//HOW DID I GET HERE
		cerr << "HOW DID I GET HERE?" << endl;
	}
}

/*
 * Function: Return:generate
 *
 * Description: Generate "return" control flow operator ASM code
 */

void Return::generate()
{
	//
	//By Covention, return values are stored in %eax
	//
	
	//Do other generations
	_expr -> generate();
	cerr << "This is the return statement: " << _expr << endl;

	//Generate Temp Variable Offset
	//Unnecessry, Return is a statment not an expression
	
	//Load
	cout << "\tmovl\t" << _expr << ", %eax" << endl;
	//Op
	cout << "\tjmp\t" << *returnLabel  << endl; 
	//Store
	//Do nothing, temp variable is already in %eax
	//_expr -> _operand = "%eax";
}

/*
 * Function: While::generate
 *
 * Description: Generate "while" control flow operator ASM code
 */

void While::generate()
{
	//Create new Labels
	Label topOfLoop;
	Label exitLoop;

	cout << topOfLoop << ":" << endl;
	//Do other generations
	_expr -> generate();

	//Start Loop and Make Conditional Check
	cout << "\tcmpl\t$0, " << _expr << endl;
	//Jump if equal
	cout << "\tje\t" << exitLoop << endl;

	//Generate _stmt
	_stmt -> generate();
	//Jump back to top
	cout << "\tjmp\t" << topOfLoop << endl;

	//Print out exit label
	cout << exitLoop << ":" << endl;
}

/*
 * Function: If::generate
 *
 * Description: Generate "if" control flow operator ASM code
 */

void If::generate()
{
	//Create new Labels
	Label skipTrue;
	Label exitIfElse;

	//Do other generations
	_expr -> generate();

	//Make check against false (same regardless of existence of else statement)
	cout << "\tcmpl\t$0, " << _expr << endl;
	cout << "\tje\t" << skipTrue << endl;

	//If *_elseStmt == nullptr, then there is no else statement
	if(_elseStmt == nullptr)
	{
		//Generate _thenStmt
		_thenStmt -> generate();
		//Print Label Skip to skip over the then statement
		cout << skipTrue << ":" << endl;
	}
	//Else there is an else statement
	else
	{
		//Generate _thenStmt
		_thenStmt -> generate();

		//Jump to Exit (and over the else code)
		cout << "\tjmp\t" << exitIfElse << endl;

		//Print Label Skip to skip over the then statement to move to else statement
		cout << skipTrue << ":" << endl;

		//Generate _elseStmt
		_elseStmt -> generate();

		//Print Label Exit to skip over the else statement if then was executed
		cout<< exitIfElse << ":" << endl;
	}
}

/*
 * Function: LogicalOr::generate
 *
 * Description: Generate "logical or (||)" operand ASM code
 */

void LogicalOr::generate(){

	//Generate Temp Variable Offset
	assignTempOffset(this);
	
	//Generate Label
	Label jumpLabel;

	//Do other generation
	_left -> generate();
	
	//Comparison Operation
	cout << "\tcmpl\t$0, " << _left << endl;
	cout << "\tjne\t" << jumpLabel << endl;

	//Do other generation
	_right -> generate();

	//Comparison Operation
	cout << "\tcmpl\t$0, " << _right << endl;

	//After Compare statement	
	cout << jumpLabel << ":" << endl;
	cout << "\tsetne\t%al" << endl;
	cout << "\tmovzbl\t%al, %eax" << endl;
	cout << "\tmovl\t%eax, " << this << endl;

}

/*
 * Function: LogicalAnd::generate
 *
 * Description: Generate "logical and (&&)" operand ASM code
 */

void LogicalAnd::generate(){

	//Generate Temp Variable Offset
	assignTempOffset(this);
	
	//Generate Label
	Label jumpLabel;

	//Do other generation
	_left -> generate();
	
	//Comparison Operation
	cout << "\tcmpl\t$0, " << _left << endl;
	cout << "\tje\t" << jumpLabel << endl;

	//Do other generation
	_right -> generate();

	//Comparison Operation
	cout << "\tcmpl\t$0, " << _right << endl;

	//After Compare statement	
	cout << jumpLabel << ":" << endl;
	cout << "\tsetne\t%al" << endl;
	cout << "\tmovzbl\t%al, %eax" << endl;
	cout << "\tmovl\t%eax, " << this << endl;

}
/*
 * Function: String::generate
 *
 * Description: Generate "string" operand ASM code
 */

void String::generate(){

	stringstream ss;

	//Generate Label
	Label stringLabel;

	//Cout String
	cout << "\t.data\t" << endl;
	cout << stringLabel << ":\t.asciz " << value() << endl;
	cout << "\t.text\t" << endl;

	//Store stringLabel for use into _operand
	ss << stringLabel;
	_operand = ss.str();
}

/*
 * Function: Dereference::generate
 *
 * Description: Generate "dereference operator (*)" ASM code
 */

void Dereference::generate()
{
	//Do other generations
	_expr -> generate();
	
	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Load
	cout << "\tmovl\t" << _expr << ", %eax" << endl;

	//Start Op
	if(_type.size() == 1)
		cout << "\tmovsbl\t (%eax), %eax" << endl;
	else
		cout << "\tmovl\t (%eax), %eax" << endl;
	//End Op

	//Store
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: Dereference::generate
 *
 * Description: Generate "dereference operator (*)" ASM code
 */

void Dereference::generate(bool &indirect)
{
	//Set indirect to True
	indirect = true;	

	//Do other generations
	_expr -> generate();
	
	//Remove layer of indirection
	_operand = _expr -> _operand;	
}

/*
 * Function: Address::generate
 *
 * Description: Generate "address operator (&)" ASM code
 */

void Address::generate()
{
	//Indirect?
	bool indirect;

	//Do other generations
	_expr -> generate(indirect);
	cerr << "Address expr operand: " << _expr << endl;

	//If indirect, remove layer of indirection
	if(indirect)
	{
		_operand = _expr->_operand;
	}
	else
	{	
		//Generate Temp Variable Offset
		assignTempOffset(this);

		//Load and Op
		cout << "\tleal\t" << _expr << ", %eax" << endl;	
			
		//Store
		cout << "\tmovl\t%eax, " << this << endl;
	}
}

/*
 * Function: Field::generate
 *
 * Description: Generate "dereference operator (*)" ASM code
 */

void Field::generate()
{
	//Declare
	bool indirect = false;

	//Do other generations
	generate(indirect);

	//Load
	cout << "\tmovl\t" << this << ", %eax" << endl;
	if(_type.size() == 4)
	{
		//Offset for Int
		cout << "\tmovl\t(%eax), %eax" << endl;
	}
	else if(_type.size() == 1)
	{
		//Offset for Char
		cout << "\tmovsbl\t(%eax), %eax" << endl;
	}
	else
	{
		//Offset for WTF?
		cerr << "HOW DID I GET HERE IN FIELD?" << endl;
	}

	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Store
	cout << "\tmovl\t%eax, " << this << endl;
}

/*
 * Function: Field::generate
 *
 * Description: Generate "dereference operator (*)" ASM code
 * 				This is only if there is indirection
 */

void Field::generate(bool &indirect)
{
	//Do other generations
	_expr -> generate(indirect);
	
	//put struct reference into %eax
	if(indirect)
	{
		cout << "\tmovl\t" << _expr << ", %eax" << endl;
	}
	else
	{
		cout << "\tleal\t" << _expr << ", %eax" << endl;
	}
	
	//Generate Temp Variable Offset
	assignTempOffset(this);

	//Add offset of _id to %eax
	cout << "\taddl\t$" << _id -> symbol() -> _offset << ", %eax" << endl;

	//Move %eax -> this
	cout <<"\tmovl\t%eax, " << this << endl;

	//Set indirect to true
	indirect = true;

}
