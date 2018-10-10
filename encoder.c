/*
	x86-64 instruction encoding tools.
	Supports a very limited set of instructions.
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>


/*
	Naming conventions:

	If opcode name ends with MODRM, it uses ModR/M byte for operand encoding.
*/


// Primary register definitions

#define X86_REG_A (0) 
#define X86_REG_C (1)
#define X86_REG_D (2)
#define X86_REG_B (3)
#define X86_REG_SP (4)
#define X86_REG_BP (5)
#define X86_REG_SI (6)
#define X86_REG_DI (7)
#define X86_REG_R8 (8) 
#define X86_REG_R9 (9)
#define X86_REG_R10 (10)
#define X86_REG_R11 (11)
#define X86_REG_R12 (12)
#define X86_REG_R13 (13)
#define X86_REG_R14 (14)
#define X86_REG_R15 (15)


// Condition definitions

// Overflow
#define X86_COND_O (0)
#define X86_COND_NO (1)

// Below & Carry
#define X86_COND_B (2)
#define X86_COND_C (2)
#define X86_COND_NB (3)
#define X86_COND_NC (3)

// Equal & Zero
#define X86_COND_E (4)
#define X86_COND_Z (4)
#define X86_COND_NE (5)
#define X86_COND_NZ (5)

// Above
#define X86_COND_NA (6)
#define X86_COND_A (7)

// Sign
#define X86_COND_S (8)
#define X86_COND_NS (9)

// Parity
#define X86_COND_P (10)
#define X86_COND_NP (11)

// Less
#define X86_COND_L (12)
#define X86_COND_NL (13)

// Greater
#define X86_COND_NG (14)
#define X86_COND_G (15)

// Opcode definitions
#define X86_ADD_MODRM (0x01)
#define X86_OR_MODRM (0x09)
#define X86_ADC_MODRM (0x11)
#define X86_SBB_MODRM (0x19)
#define X86_AND_MODRM (0x21)
#define X86_SUB_MODRM (0x29)
#define X86_XOR_MODRM (0x31)
#define X86_CMP_MODRM (0x39)

#define X86_MOV_MODRM (0x89)

#define X86_MOV_REG_IMM_LONG(x) (0xB8 + (x))
#define X86_MOV_REG_IMM_LOW(x) (0xB0 + (x))

#define X86_PUSH_REG(x) (0x50 + (x))
#define X86_POP_REG(x) (0x58 + (x))

#define X86_CALL_REL32 (0xE8)
#define X86_JMP_REL32 (0xE9)

#define X86_JMP_COND_REL8(x) (0x70 + (x))


#define X86_0F (0x0F)
#define X86_0F_JMP_COND_REL32(x) (0x80 + (x))

#define X86_RET (0xC3)
#define X86_NOP (0x90)

#define X86_OPERAND_SIZE_OVERRIDE (0x66)

#define X86_FF_MODRM (0xFF)
#define X86_FF_MODRM_CALL (0x2)
#define X86_FF_MODRM_JMP (0x4)

#define X86_F7_MODRM (0xF7)
#define X86_F7_MODRM_MUL (0x4)
#define X86_F7_MODRM_IMUL (0x5)
#define X86_F7_MODRM_DIV (0x6)
#define X86_F7_MODRM_IDIV (0x7)

// REX prefix

#define X86_REX (0x40)
#define X86_REX_B (0x1)
#define X86_REX_X (0x2)
#define X86_REX_R (0x4)
#define X86_REX_W (0x8)

#define X86_REX_RM (0x1)
#define X86_REX_SIB (0x2)
#define X86_REX_REG (0x4)
#define X86_REX_WIDE (0x8)

// Macro for REX prefix and flags

#define X86_REX_FIELD(b,x,r,w) (X86_REX | \
	((b)?X86_REX_B:0) |\
	((x)?X86_REX_X:0) |\
	((r)?X86_REX_R:0) |\
	((w)?X86_REX_W:0))

//ModR/M field

struct x86_modrm
{
	int rm : 3;
	int reg : 3;
	int mod : 2;
} __attribute__((packed));



// Relocation information structure, used with labels, jumps and calls
struct x86_relocation
{
	size_t offset; //Offset of relocation in bytecode
	int label; //Label to relocate to
	int relative; //Whether relocation is absolute or relative
};


// Maintains internal encoder state. memset to zero for safe initial conditions
struct x86_encoder
{
	char* buffer; //Encoded bytecode buffer
	size_t buffer_size; //Current size of bytecode
	size_t buffer_capacity; //Capacity of bytecode buffer
	
	size_t* labels; //Labels array, contains offsets to bytecode positions
	size_t labels_size;
	size_t labels_capacity;
	
	struct x86_relocation* relocations; //Relocations information
	size_t relocations_size;
	size_t relocations_capacity;
};

void x86_encoder_free(struct x86_encoder* enc)
{
	free(enc->buffer);
	free(enc->labels);
	free(enc->relocations);
	memset(enc, 0, sizeof *enc);
}



// Macro for quickly accessing and writing to bytecode buffer
#define ENC_X(enc, p) ((enc)->buffer[(enc)->buffer_size + (p)])

// Macro for advancing the bytecode buffer
#define ENC_ADVANCE(enc, amount) do { (enc)->buffer_size += amount; } while(0)

// Checks if there's enough capacity in the bytecode buffer for required bytes
void x86_encoder_check_buffer(struct x86_encoder* enc, size_t required)
{
	if (enc->buffer_size + required > enc->buffer_capacity) {
		enc->buffer_capacity += required + 1024; 
		enc->buffer = realloc(enc->buffer, enc->buffer_capacity); 
	}
}

// Moves label to current position in bytecode buffer
void x86_encoder_move_label(struct x86_encoder* enc, size_t label)
{
	enc->labels[label] = enc->buffer_size;
}

// Adds label to current position in bytecode buffer. Label id is returned
size_t x86_encoder_add_label(struct x86_encoder* enc)
{
	size_t nlabel = enc->labels_size;
	if (enc->labels_size >= enc->labels_capacity) {
		enc->labels_capacity += 1024;
		enc->labels = realloc(enc->labels, enc->labels_capacity * sizeof *enc->labels);
	}
	enc->labels_size += 1;
	enc->labels[nlabel] = enc->buffer_size;
	return nlabel;
}

// Adds a relocation to current position in bytecode buffer
void x86_encoder_add_relocation(struct x86_encoder* enc, int label, int relative)
{
	if (enc->relocations_size >= enc->relocations_capacity) {
		enc->relocations_capacity += 1024;
		enc->relocations = realloc(enc->relocations, enc->relocations_capacity * sizeof *enc->relocations);
	}
	enc->relocations[enc->relocations_size].offset = enc->buffer_size;
	enc->relocations[enc->relocations_size].label = label;
	enc->relocations[enc->relocations_size].relative = relative;
	enc->relocations_size += 1;
}

// Relocates instructions to new base address. Assumes t_buffer contains the bytecode to modify
// If code consists only of relative addressing, base is not required
int x86_encoder_apply_relocations_in_memory(struct x86_encoder* enc, char* t_buffer, size_t base)
{
	for (size_t i = 0; i < enc->relocations_size; i++) {
		struct x86_relocation* reloc = enc->relocations + i;
		size_t label = reloc->label;
		if (label >= enc->labels_size)
			return 1;
		intptr_t to = (intptr_t) (enc->labels[label]);
		if (reloc->relative) {
			intptr_t from = (intptr_t) (reloc->offset + 4);
			int32_t* offset = (int32_t*)(t_buffer + reloc->offset);
			int32_t rel = to - from;
			(*offset) = rel;
		} else {
			uint64_t* offset = (uint64_t*)(t_buffer + reloc->offset);
			(*offset) = base + to;
		}
	}
	return 0;
}

// Relocates instructions to new base address
// If code consists only of relative addressing, base is not required
int x86_encoder_apply_relocations(struct x86_encoder* enc, size_t base)
{
	return x86_encoder_apply_relocations_in_memory(enc, enc->buffer, base);
}


// Copy and prepare byte code to target memory address
// target memory should be large enough to fully contain encoded bytecode
int x86_encoder_link_to_memory(struct x86_encoder* enc, char* target)
{
	memcpy(target, enc->buffer, enc->buffer_size);
	return x86_encoder_apply_relocations_in_memory(enc, target, (size_t)(target));
}

// Helper functions for encoding ModR/M based instructions

void _x86_encoder_prepare_modrm_rex(struct x86_encoder* enc, char opcode, char rm, char reg, int wide)
{
	ENC_X(enc, 0) = X86_REX_FIELD(rm & 0x08, 0, reg & 0x08, wide);
	ENC_X(enc, 1) = opcode;
	struct x86_modrm* modrm = ((struct x86_modrm*)&ENC_X(enc, 2));
	modrm->rm = rm & 0x07;
	modrm->reg = reg & 0x07;
	modrm->mod = 0x03;
}



void x86_encoder_write_modrm_rex(struct x86_encoder* enc, char opcode, char rm, char reg, int wide)
{
	x86_encoder_check_buffer(enc, 3);
	_x86_encoder_prepare_modrm_rex(enc, opcode, rm, reg, wide);
	ENC_ADVANCE(enc, 3);
}

// General instruction encoding functions

void x86_encoder_write_jmp(struct x86_encoder* enc, int call, size_t label)
{
	x86_encoder_check_buffer(enc, 5);
	char opcode;
	if (call)
		opcode = X86_CALL_REL32;
	else
		opcode = X86_JMP_REL32;
	
	
	ENC_X(enc, 0) = opcode;
	ENC_ADVANCE(enc, 1);
	
	
	*(uint32_t*)(enc->buffer + enc->buffer_size) = 0;
	
	x86_encoder_add_relocation(enc, label, 1);
	ENC_ADVANCE(enc, 4);
}



void x86_encoder_write_jmp_cond(struct x86_encoder* enc, int cond, size_t label)
{
	x86_encoder_check_buffer(enc, 6);
	
	ENC_X(enc, 0) = X86_0F;
	ENC_X(enc, 1) = X86_0F_JMP_COND_REL32(cond);
	ENC_ADVANCE(enc, 2);
	
	*(uint32_t*)&ENC_X(enc, 0) = 0;
	
	x86_encoder_add_relocation(enc, label, 1);
	ENC_ADVANCE(enc, 4);
}


void x86_encoder_write_jmp_reg(struct x86_encoder* enc, int call, char reg)
{
	x86_encoder_write_modrm_rex(enc, X86_FF_MODRM, reg, call ? X86_FF_MODRM_CALL : X86_FF_MODRM_JMP, 1);
}

void x86_encoder_write_cmp_reg(struct x86_encoder* enc, char reg_1, char reg_2)
{
	x86_encoder_write_modrm_rex(enc, X86_CMP_MODRM, reg_1, reg_2, 1);
}


void x86_encoder_write_modrm(struct x86_encoder* enc, char opcode, char reg_1, char reg_2)
{
	x86_encoder_write_modrm_rex(enc, opcode, reg_1, reg_2, 1);
}

void x86_encoder_write_modrm_32(struct x86_encoder* enc, char opcode, char reg_1, char reg_2)
{
	x86_encoder_write_modrm_rex(enc, opcode, reg_1, reg_2, 0);
}

void x86_encoder_write_modrm_16(struct x86_encoder* enc, char opcode, char reg_1, char reg_2)
{
	x86_encoder_check_buffer(enc, 4);
	ENC_X(enc, 0) = X86_OPERAND_SIZE_OVERRIDE;
	ENC_ADVANCE(enc, 1);
	_x86_encoder_prepare_modrm_rex(enc, opcode, reg_1, reg_2, 0);
	ENC_ADVANCE(enc, 3);
}

void x86_encoder_write_modrm_8(struct x86_encoder* enc, char opcode, char reg_1, char reg_2)
{
	x86_encoder_write_modrm_rex(enc, opcode - 1, reg_1, reg_2, 0);
}

void x86_encoder_write_mov_imm_64(struct x86_encoder* enc, char reg, uint64_t value)
{
	x86_encoder_check_buffer(enc, 11);
	ENC_X(enc, 0) = X86_REX_FIELD(reg & 0x08, 0, 0, 1);
	ENC_X(enc, 1) = X86_MOV_REG_IMM_LONG(reg & 0x07);
	*(uint64_t*)(&ENC_X(enc,2)) = value;
	ENC_ADVANCE(enc, 2 + 8);
}


void x86_encoder_write_mov_imm_32(struct x86_encoder* enc, char reg, uint32_t value)
{
	x86_encoder_check_buffer(enc, 6);
	ENC_X(enc, 0) = X86_REX_FIELD(reg & 0x08, 0, 0, 0);
	ENC_X(enc, 1) = X86_MOV_REG_IMM_LONG(reg & 0x07);
	*(uint32_t*)(&ENC_X(enc,2)) = value;
	ENC_ADVANCE(enc, 2 + 4);
}

void x86_encoder_write_mov_imm_16(struct x86_encoder* enc, char reg, uint16_t value)
{
	x86_encoder_check_buffer(enc, 5);
	ENC_X(enc, 0) = X86_OPERAND_SIZE_OVERRIDE;
	ENC_X(enc, 1) = X86_REX_FIELD(reg & 0x08, 0, 0, 0);
	ENC_X(enc, 2) = X86_MOV_REG_IMM_LONG(reg & 0x07);
	*(uint16_t*)(&ENC_X(enc, 3)) = value;
	ENC_ADVANCE(enc, 3 + 2);
}

void x86_encoder_write_mov_imm_8(struct x86_encoder* enc, char reg, uint8_t value)
{
	x86_encoder_check_buffer(enc, 3);
	ENC_X(enc, 0) = X86_REX_FIELD(reg & 0x08, 0, 0, 0);
	ENC_X(enc, 1) = X86_MOV_REG_IMM_LOW(reg & 0x07);
	*(uint8_t*)(&ENC_X(enc, 2)) = value;
	ENC_ADVANCE(enc, 2 + 1);
}

void x86_encoder_write_push(struct x86_encoder* enc, char reg)
{
	x86_encoder_check_buffer(enc, 2);
	ENC_X(enc, 0) = X86_REX_FIELD(reg & 0x08, 0, 0, 0);
	ENC_X(enc, 1) = X86_PUSH_REG(reg & 0x07);
	ENC_ADVANCE(enc, 2);
}

void x86_encoder_write_pop(struct x86_encoder* enc, char reg)
{
	x86_encoder_check_buffer(enc, 2);
	ENC_X(enc, 0) = X86_REX_FIELD(reg & 0x08, 0, 0, 0);
	ENC_X(enc, 1) = X86_POP_REG(reg & 0x07);
	ENC_ADVANCE(enc, 2);
}

void x86_encoder_write_ret(struct x86_encoder* enc)
{
	x86_encoder_check_buffer(enc, 1);
	ENC_X(enc, 0) = X86_RET;
	ENC_ADVANCE(enc, 1);
}

void x86_encoder_write_nop(struct x86_encoder* enc)
{
	x86_encoder_check_buffer(enc, 1);
	ENC_X(enc, 0) = X86_NOP;
	ENC_ADVANCE(enc, 1);
}


int main(int argc, const char** argv)
{
	struct x86_encoder enc;

	//zero out encoder for safe initial state
	memset(&enc, 0, sizeof enc);

	// Our intention is to write the following function in assembly
	/*
		long factorial(long p) {
			long ret = 1;
			while (p > 0) {
				ret = ret * p;
				p -= 1;
			}
			return ret;
		}
	*/

	//we need two labels
	size_t label_start = x86_encoder_add_label(&enc);
	size_t label_end = x86_encoder_add_label(&enc);

	//input argument is in RDI
	//initialize values

	//zero rax and set low byte to 0x01
	x86_encoder_write_modrm(&enc, X86_XOR_MODRM ,X86_REG_A, X86_REG_A);
	x86_encoder_write_mov_imm_8(&enc, X86_REG_A, 0x01);

	//copy from rax
	x86_encoder_write_modrm(&enc, X86_MOV_MODRM ,X86_REG_R8, X86_REG_A);

	//loop start label here
	x86_encoder_move_label(&enc, label_start);

	//zero out RDX (immediate mode comparison isn't supported)
	x86_encoder_write_modrm(&enc, X86_XOR_MODRM ,X86_REG_D, X86_REG_D);
	//compare RDI and RDX
	x86_encoder_write_modrm(&enc, X86_CMP_MODRM ,X86_REG_DI, X86_REG_D);
	//if RDI <= RDX, jump to the end label
	x86_encoder_write_jmp_cond(&enc, X86_COND_NG, label_end);

	//ret = ret * p, single operand IMUL places result automatically to RAX
	x86_encoder_write_modrm(&enc, X86_F7_MODRM, X86_REG_DI, X86_F7_MODRM_IMUL);

	//immediate arithmetic isn't supported so we use another register for subtraction
	//p -= 1,  R8 was initialized to 1
	x86_encoder_write_modrm(&enc, X86_SUB_MODRM ,X86_REG_DI, X86_REG_R8);
	//and jump to loop start
	x86_encoder_write_jmp(&enc, 0, label_start);
	//end label
	x86_encoder_move_label(&enc, label_end);
	//value returned is in returned in RAX
	x86_encoder_write_ret(&enc);


	//allocate executable memory
	char* target_mem = mmap(0, enc.buffer_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	//link and copy our code to newly allocated memory
	int res = x86_encoder_link_to_memory(&enc, target_mem);

	printf("Linking result: %d\n", res);

	long(*func)(long) = (void*)target_mem;

	//and test
	for (int i = 0; i < 15; i++) {
		printf("func(%d) == %d\n", i, func(i));
	}

	munmap(target_mem, enc.buffer_size);


	// Some garbage for testing 
	if (0)
	{
		x86_encoder_write_mov_imm_64(&enc, X86_REG_A, 0xdeadbeef12345678);
		x86_encoder_write_mov_imm_64(&enc, X86_REG_R9, 0xdeadbeef12345678);
		x86_encoder_write_mov_imm_32(&enc, X86_REG_R9, 0x12345678);
		x86_encoder_write_mov_imm_16(&enc, X86_REG_R9, 0x1234);
		
		for (int i = 0; i < 16; i++)
			x86_encoder_write_mov_imm_8(&enc, i, 0x12);
		
		x86_encoder_write_modrm(&enc, X86_ADD_MODRM ,X86_REG_A, X86_REG_D);
		x86_encoder_write_modrm(&enc, X86_CMP_MODRM ,X86_REG_A, X86_REG_D);
		x86_encoder_write_modrm(&enc, X86_MOV_MODRM ,X86_REG_A, X86_REG_D);
		x86_encoder_write_modrm_32(&enc, X86_MOV_MODRM ,X86_REG_A, X86_REG_D);
		x86_encoder_write_modrm_16(&enc, X86_MOV_MODRM ,X86_REG_A, X86_REG_D);
		x86_encoder_write_modrm_8(&enc, X86_MOV_MODRM ,X86_REG_A, X86_REG_D);

		x86_encoder_write_modrm(&enc, X86_F7_MODRM, X86_REG_R9, X86_F7_MODRM_IMUL);
		x86_encoder_write_jmp_reg(&enc, 1, X86_REG_A);
		
		size_t label_test2 = x86_encoder_add_label(&enc);
		x86_encoder_write_modrm(&enc, X86_CMP_MODRM ,X86_REG_A, X86_REG_D);
		x86_encoder_write_jmp_cond(&enc, X86_COND_E, label_test2);
		x86_encoder_write_jmp(&enc, 0, label_test2);
		
		x86_encoder_move_label(&enc, label_test2);
		x86_encoder_write_nop(&enc);
	}

	//write test binary for easier debugging: ndisasm -b 64 test_binary
	x86_encoder_apply_relocations(&enc, 0);
	FILE* file = fopen("test_binary", "wb");

	fwrite(enc.buffer, 1, enc.buffer_size, file);

	fclose(file);

	x86_encoder_free(&enc);
	
	return 0;
}
