/*
	Pseudo-intermediate representation
*/

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/*
	Dynamic array macros

	array: a pointer of type T to the array memory
	size: current size of the array
	capacity: current capacity of the array
	capacity_increment: how much to increment the capacity
		when running out of space

	Usage is something like:

		struct MyDoubleArray {
			double* data;
			size_t size;
			size_t capacity;
		};

		int main() {
			struct MyDoubleArray arr;
			memset(&arr, 0, sizeof arr);
			DYNAMIC_ARRAY_RESERVE(arr.data, arr.size, arr.capacity, 5);
			for (double d = 2.0; d < 1e7; d = d * 2)
				DYNAMIC_ARRAY_PUSH(arr.data, arr.size, arr.capacity, d, 100);

			DYNAMIC_ARRAY_RESIZE(arr.data, arr.size, arr.capacity, 10);
			for (size_t i = 0; i < arr.size; i++)
				printf("%f\n", arr.data[i]);
			DYNAMIC_ARRAY_FREE(arr.data, arr.size, arr.capacity);
			return 0;
		}

*/

#define DYNAMIC_ARRAY_RESERVE(array, size, capacity, amount) \
do { \
	if (amount >= capacity) { \
		capacity = amount; \
		array = realloc(array, capacity * sizeof * array); \
	} \
} while (0)

#define DYNAMIC_ARRAY_RESIZE(array, size, capacity, newsize) \
do { \
	DYNAMIC_ARRAY_RESERVE(array, size, capacity, newsize); \
	size = newsize; \
} while (0)

#define DYNAMIC_ARRAY_PUSH(array, size, capacity, item, capacity_increment) \
do { \
	if (size >= capacity) { \
		capacity += capacity_increment; \
		array = realloc(array, capacity * sizeof * array); \
	} \
	size += 1; \
	array[size - 1] = item;\
} while (0)

#define DYNAMIC_ARRAY_FREE(array, size, capacity) \
do { \
	size = 0; \
	capacity = 0; \
	free(array); \
	array = 0; \
} while (0)



#define IR_TYPE_VOID 0
#define IR_TYPE_U64 1 
#define IR_TYPE_I64 2
#define IR_TYPE_U32 3
#define IR_TYPE_I32 4
#define IR_TYPE_U16 5 
#define IR_TYPE_I16 6
#define IR_TYPE_U8 7
#define IR_TYPE_I8 8
#define IR_TYPE_F64 9
#define IR_TYPE_F32 10
#define IR_TYPE_STRUCT 11

#define OPERAND_INFO_TYPE_IMMEDIATE (0)
#define OPERAND_INFO_TYPE_VARIABLE (1)
#define OPERAND_INFO_TYPE_ARGUMENT (2)
#define OPERAND_INFO_TYPE_CONSTANT (3)
#define OPERAND_INFO_TYPE_FUNCTION (4)
#define OPERAND_FLAG_ADDRESS (1<<0)
#define OPERAND_FLAG_DEREFERENCE (1<<1)

#define OPCODE_NOP 0
#define OPCODE_COPY 1
#define OPCODE_ADD 2
#define OPCODE_SUB 3
#define OPCODE_MUL 4
#define OPCODE_DIV 5

#define OPCODE_NOT 6
#define OPCODE_OR 7
#define OPCODE_AND 8
#define OPCODE_BIT_NEG 9
#define OPCODE_BIT_OR 10
#define OPCODE_BIT_AND 11
#define OPCODE_BIR_XOR 12

#define OPCODE_BIT_SHIFT_LEFT 13
#define OPCODE_BIT_SHIFT_LOGICAL_RIGHT 14
#define OPCODE_BIT_SHIFT_ARITHMETIC_RIGHT 15

#define OPCODE_GOTO_BASE 16
#define OPCODE_GOTO_COND(comparison_type) (OPCODE_GOTO_BASE + (comparison_type))
#define OPCODE_COMPARE_BASE 24
#define OPCODE_COMPARE(comparison_type) (OPCODE_COMPARE_BASE + (comparison_type))

#define OPCODE_SET_ARGUMENT 32
#define OPCODE_CALL 33
#define OPCODE_RETURN 34

#define COMPARISON_ALWAYS 0
#define COMPARISON_EQUAL 1
#define COMPARISON_NOT_EQUAL 2
#define COMPARISON_LESS 3
#define COMPARISON_GREATER 4
#define COMPARISON_LEQUAL 5
#define COMPARISON_GEQUAL 6

struct TypeInfo
{
	unsigned short type;
	unsigned short sub_type;
	size_t struct_size;
};

struct Operand
{
	union
	{
		int ref_id;
		uint64_t value_u64;
		int64_t value_i64;
		uint32_t value_u32;
		int32_t value_i32;
		uint16_t value_u16;
		int16_t value_i16;
		uint8_t value_u8;
		int8_t value_i8;
		double value_f64;
		float value_f32;
	};
	unsigned short info_type;
	unsigned short info_flags;
	struct TypeInfo type_info;
};

#define OPERAND_TARGET (0)
#define OPERAND_PRIMARY_1 (1)
#define OPERAND_PRIMARY_2 (2)

struct Opcode 
{
	int type;
	struct Operand operands[3];
};

struct Variable
{
	struct TypeInfo type_info;
};

struct Function
{
	int id;
	
	struct TypeInfo* arguments;
	size_t arguments_size;

	struct TypeInfo return_type;

	struct Opcode* opcodes;
	size_t opcodes_size;
	size_t opcodes_capacity;

	struct Variable* variables;
	size_t variables_size;
	size_t variables_capacity;
};

struct OpcodeInfo
{
	int previous_label;
	int jump_from;
};

#define VARIABLE_INFO_PRUNED (1<<0)
#define VARIABLE_INFO_UNUSED (1<<1)
#define VARIABLE_INFO_ETERNAL (1<<2)
#define VARIABLE_INFO_UNINITIALIZED (1<<4)

struct VariableInfo
{
	int lifetime_start;
	int lifetime_end;
	unsigned flags;
};

struct FunctionAnalysis
{
	struct Function* function;
	struct OpcodeInfo* infos;
	struct VariableInfo* variables;
};

int opcode_is_jump(struct Opcode* x) {
	return (x->type >= OPCODE_GOTO_BASE) && (x->type < OPCODE_GOTO_BASE + 8);
}

int opcode_is_pure_assignment(struct Opcode* x) {
	if (x->type == OPCODE_COPY)
		return 1;
	if (x->type == OPCODE_CALL)
		return 1;
	return 0;
}

int opcode_modifies_target_operand(struct Opcode* x) {
	if (x->type >= 1 && x->type <= 15)
		return 1;
	if (x->type >= OPCODE_COMPARE_BASE && x->type < OPCODE_COMPARE_BASE + 8)
		return 1;
	if (x->type == OPCODE_CALL)
		return 1;
	return 0;
}

int opcode_read_operand_primary_1(struct Opcode* x) {
	if (x->type == OPCODE_COMPARE_BASE || x->type == OPCODE_GOTO_BASE)
		return 0;
	if (x->type == OPCODE_NOP)
		return 0;
	return 1;
}

int opcode_read_operand_primary_2(struct Opcode* x) {
	if (!opcode_read_operand_primary_1(x))
		return 0;
	if (x->type == OPCODE_RETURN)
		return 0;
	if (x->type == OPCODE_CALL)
		return 0;
	if (x->type == OPCODE_SET_ARGUMENT)
		return 0;
	if (x->type == OPCODE_BIT_NEG)
		return 0;
	if (x->type == OPCODE_NOT)
		return 0;
	if (x->type == OPCODE_COPY)
		return 0;
	return 1;
}


/*
	Variable lifetime calculations are performed in rather simple and 
	maybe naive manner. The first phase is to find first and last
	instructions that reference the variable. However this type of thinking
	forgets that jumps exists: a goto instruction after the last instruction
	referencing the variable might jump to a point where the variable is 
	alive. The variable would be "dead" when the goto instruction is
	executed, and the result would be invalid.

	The simplest solution is to extend the lifetime to include the goto
	instruction. Jumps before the lifetime starts doesn't need to be
	accounted for unless one wants to track "possibly uninitialised"
	status of the variables.

	Additionally when the address of a variable is taken the automatically
	marks the variable as "eternal" with infinite lifetime.

	Surely more sophisticated algorithms exist for lifetime tracking but
	this is more simple and concise, while non-optimal. 
*/

void extend_variable_lifetime(struct FunctionAnalysis* a, struct VariableInfo* var, int index, int pure_assignment) {
	if (var->lifetime_end >= index || (var->flags & VARIABLE_INFO_ETERNAL) || (var->flags & VARIABLE_INFO_UNINITIALIZED))
		return;

	if (var->lifetime_start == -1) {
		//No jump scanning needs to be performed on the first use of the variable
		//after all the lifetime begins from this very instruction
		if (pure_assignment) {
			var->lifetime_start = index;
			var->flags |= VARIABLE_INFO_UNUSED;
			var->lifetime_end = index + 1;
			return;
		} else {
			//Variable is used before first assignment
			//flag as eternal and uninitialized
			var->flags |= VARIABLE_INFO_ETERNAL | VARIABLE_INFO_UNINITIALIZED;
			return;
		}
	}

	if (pure_assignment)
		var->flags |= VARIABLE_INFO_UNUSED;
	else
		var->flags &= ~VARIABLE_INFO_UNUSED;

	//Scan for instruction that jump into current lifetime
	//and include them in lifetime

	int maximum;
	int minimum = var->lifetime_end;
	if (minimum < var->lifetime_start)
		minimum = var->lifetime_start;
	
	int max_jmp_pos = index;
	do {
		maximum = max_jmp_pos + 1;
		int pos = max_jmp_pos;
		while (pos >= minimum) {
			if (a->infos[pos].jump_from > max_jmp_pos) {
				max_jmp_pos = a->infos[pos].jump_from;
			}
			pos = a->infos[pos].previous_label;
		}
		minimum = maximum;
	} while(max_jmp_pos >= maximum);

	var->lifetime_end = maximum;
}

int operand_is_variable(struct Operand* operand)
{
	return (operand->info_type == OPERAND_INFO_TYPE_VARIABLE);
}

int operand_is_variable_address_load(struct Operand* operand)
{
	return (operand_is_variable(operand) && (operand->info_flags |OPERAND_FLAG_ADDRESS));
}

void analyse_function(struct Function* fn)
{
	struct FunctionAnalysis* a = malloc(sizeof *a);
	memset(a, 0, sizeof *a);

	a->infos = malloc(fn->opcodes_size * sizeof *a->infos);
	a->variables = malloc(fn->variables_size * sizeof *a->variables);

	//Initialize variables
	for (size_t i = 0; i < fn->variables_size; ++i) {
		a->variables[i].lifetime_start = -1;
		a->variables[i].lifetime_end = -1;
		a->variables[i].flags = 0;
	}

	//Generate label data
	for (int i = fn->opcodes_size - 1; i >= 0; --i) {
		struct Opcode* op = fn->opcodes + i;
		if (opcode_is_jump(op)) {
			int label = op->operands[0].ref_id;
			if (a->infos[label].jump_from >= 0)
				continue;
			a->infos[label].jump_from = i;
		}
	}

	//Build previous label list
	int previous_label = -1;
	for (size_t i = 0; i < fn->opcodes_size; ++i) {
		a->infos[i].previous_label = previous_label;
		if (a->infos[i].jump_from >= 0)
			previous_label = i;
	}

	//Calculate variable lifetimes
	for (size_t i = 0; i < fn->opcodes_size; ++i) {
		struct Opcode* op = &fn->opcodes[i];
		int pure_assign = opcode_is_pure_assignment(op);

		if (operand_is_variable(&op->operands[OPERAND_TARGET]) && (pure_assign || opcode_modifies_target_operand(op)))
			extend_variable_lifetime(a, &a->variables[op->operands[OPERAND_TARGET].ref_id], i, pure_assign);

		if (operand_is_variable_address_load(&op->operands[OPERAND_PRIMARY_1]))
			a->variables[op->operands[OPERAND_PRIMARY_1].ref_id].flags |= VARIABLE_INFO_ETERNAL;
		else if (operand_is_variable(&op->operands[OPERAND_PRIMARY_1]) && opcode_read_operand_primary_1(op))
			extend_variable_lifetime(a, &a->variables[op->operands[OPERAND_PRIMARY_1].ref_id], i, 0);

		if (operand_is_variable_address_load(&op->operands[OPERAND_PRIMARY_2]))
			a->variables[op->operands[OPERAND_PRIMARY_2].ref_id].flags |= VARIABLE_INFO_ETERNAL;
		else if (operand_is_variable(&op->operands[OPERAND_PRIMARY_2]) && opcode_read_operand_primary_2(op))
			extend_variable_lifetime(a, &a->variables[op->operands[OPERAND_PRIMARY_2].ref_id], i, 0);
	}

	free(a);
}