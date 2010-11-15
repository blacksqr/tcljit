#ifndef TCLJIT_X86_MCODE_H
#define TCLJIT_X86_MCODE_H

#include "tclJitCodeGen.h"

typedef enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI } regs;

void initLabelTable(void);
void finishLabelTable(void);
void jumpEqual(struct MCode *, const char *);
void jumpNotEqual(struct MCode *, const char *);
void jumpTo(struct MCode *, const char *);
char *uniqueLabel(void);
void makeLabel(struct MCode *, const char *);
//inline int load_localvar(struct MCode *, struct Quadruple *);
inline void load_localvar(struct MCode *, struct Quadruple *, int);
inline void load_tclobject(struct MCode *, int, int);

/* Auxiliary macros. Most of these may need to be ported to
 * different instruction sets. */
#define INDEX_TO_STACKADDR(index) (-((index + 1) * 4))

#define IMM32(code, v) \
    *code++ = v; *code++ = v >> 8; *code++ = v >> 16; *code++ = v >> 24

#define PROLOGUE(code, stsize) { \
    PUSH_REG(code, EBP); MOV_REG_REG(code, ESP, EBP); \
    if (4 * stsize > 0) { \
        if (4 * stsize > 127) { SUB_IMM32_REG(code, 4 * stsize, ESP); } \
        else { SUB_IMM8_REG(code, 4 * stsize, ESP); } \
    } \
}

#define EPILOGUE(code, stsize) { \
    if (4 * stsize > 0) { \
        if (4 * stsize > 127) { ADD_IMM32_REG(code, 4 * stsize, ESP); } \
        else { ADD_IMM8_REG(code, 4 * stsize, ESP); } \
    } \
    LEAVE(code); RETN(code); \
}

#define LABEL(mcode, label) makeLabel(mcode, label)

/* Copy parameter n at 8+4*n(%EBP) to a register. n starts in 0. */
#define COPY_PARAM_REG(code, paramn, reg) \
    MOV_DISP8DREG_REG(code, 8 + 4*paramn, EBP, reg)

/* Some OSes may not require this alignment. */
#define ALIGN_STACK(code) AND_IMM8_REG(code, -16, ESP)

#define MODRM(mod, reg, rm) (mod << 6) + (reg << 3) + rm


/* Instructions. */
#define NOP(code) *code++ = 0x90

#define INC_REG(code, reg) *code++ = 0x40 + reg
#define INC_DREG(code, reg) \
    *code++ = 0xFF; \
    *code++ = MODRM(0x0, 0, reg)
#define DEC_REG(code, reg) *code++ = 0x48 + reg
#define DEC_DREG(code, reg) \
    *code++ = 0xFF; \
    *code++ = MODRM(0x0, 1, reg)

#define PUSH_DISP8DREG(code, disp, reg) \
    *code++ = 0xFF; \
    *code++ = MODRM(0x1, 6, reg); \
    *code++ = disp
#define PUSH_DISP32DREG(code, disp, reg) \
    *code++ = 0xFF; \
    *code++ = MODRM(0x2, 6, reg); \
    IMM32(code, disp)
#define PUSH_REG(code, reg) *code++ = 0x50 + reg
#define PUSH_DREG(code, reg) *code++ = 0xFF; *code++ = MODRM(0, 6, reg)
#define POP_REG(code, reg) *code++ = 0x58 + reg

/* XXX Se tirar o parenteses mais externo em ((unsighed char*) ... )
 * o gcc da um warning legal :) */
#define CALL(code, func) do { /* Not used yet. */ \
    *code++ = 0xE8; \
    IMM32(code, ((unsigned char*)func - code - 5)); \
} while (0)

#define CALL_ABSOLUTE_REG(code, reg) \
    *code++ = 0xFF; \
    *code++ = MODRM(0x3, 2, reg)

/* cmpl %src, %dest */
#define CMP_REG_REG(code, src, dest) \
    *code++ = 0x39; \
    *code++ = MODRM(0x3, src, dest)
/* cmpl %src, %(dest) */
#define CMP_REG_DREG(code, src, dest) \
    *code++ = 0x39; \
    *code++ = MODRM(0, src, dest)
/* cmpl $imm32, %reg */
#define CMP_IMM32_REG(code, imm, reg) \
    *code++ = 0x81; \
    *code++ = MODRM(0x3, 7, reg); \
    IMM32(code, imm)
#define CMP_IMM8_REG(code, imm, reg) \
    *code++ = 0x83; \
    *code++ = MODRM(0x3, 7, reg); \
    *code++ = imm

/* imul %src, %dest */ /* XXX Check MODRM order of src, dest */
#define MUL_REG_REG(code, src, dest) \
    *code++ = 0x0F; \
    *code++ = 0xAF; \
    *code++ = MODRM(0x3, src, dest)
/* imul %src, $imm, %dest */
#define MUL_IMM32_REG_REG(code, imm32, src, dest) \
    *code++ = 0x69; \
    *code++ = MODRM(0x3, dest, src); \
    IMM32(code, imm32)
/* imul %reg */
#define MUL_REG(code, reg) \
    *code++ = 0xF7; \
    *code++ = MODRM(0x3, 5, reg)
/* imul (%reg) */
#define MUL_DREG(code, reg) \
    *code++ = 0xF7; \
    *code++ = MODRM(0, 5, reg)

/* idiv %reg */
#define DIV_REG(code, reg) \
    *code++ = 0xF7; \
    *code++ = MODRM(0x3, 7, reg)

/* XXX Fixed a bug here, was using 0x33 instead (and saving
 * the result in the source register instead of dest reg).
 * Note: Always be twice careful with machine code. */
#define XOR_REG_REG(code, src, dest) \
    *code++ = 0x31; \
    *code++ = MODRM(0x3, src, dest)
#define XOR_REG_DREG(code, src, dest) \
    *code++ = 0x31; \
    *code++ = MODRM(0, src, dest)

#define AND_IMM8_REG(code, imm, reg) \
    *code++ = 0x83; \
    *code++ = MODRM(0x3, 4, reg); \
    *code++ = imm

/* addl (%src), %dest */
#define ADD_DREG_REG(code, src, dest) \
    *code++ = 0x03; \
    *code++ = MODRM(0, src, dest)

/* addl %src, (%dest) */
#define ADD_REG_DREG(code, src, dest) \
    *code++ = 0x01; \
    *code++ = MODRM(0, src, dest)

/* addl %src, %dest */
#define ADD_REG_REG(code, src, dest) \
    *code++ = 0x01; \
    *code++ = MODRM(0x3, src, dest)

/* addl $imm8, %reg */
#define ADD_IMM8_REG(code, imm, reg) \
    *code++ = 0x83; \
    *code++ = MODRM(0x3, 0, reg); \
    *code++ = imm

#define ADD_IMM32_REG(code, imm, reg) \
    *code++ = 0x81; \
    *code++ = MODRM(0x3, 0, reg); \
    IMM32(code, imm)

/* leal imm8(%src), %dest */
#define LEA_DISP8DREG_REG(code, disp, src, dest) \
    *code++ = 0x8d; \
    *code++ = MODRM(0x1, dest, src); \
    *code++ = disp
/* leal imm32(%src), %dest */
#define LEA_DISP32DREG_REG(code, disp, src, dest) \
    *code++ = 0x8d; \
    *code++ = MODRM(0x2, dest, src); \
    IMM32(code, disp)
/* leal (%src), %dest */
#define LEA_DREG_REG(code, src, dest) \
    *code++ = 0x8d; \
    *code++ = MODRM(0, dest, src)

#define SUB_IMM8_REG(code, imm, reg) \
    *code++ = 0x83; \
    *code++ = MODRM(0x3, 0x5, reg); \
    *code++ = imm
#define SUB_IMM32_REG(code, imm, reg) \
    *code++ = 0x81; \
    *code++ = MODRM(0x3, 0x5, reg); \
    IMM32(code, imm)
#define SUB_REG_DREG(code, src, dest) \
    *code++ = 0x29; \
    *code++ = MODRM(0, src, dest)
#define SUB_REG_REG(code, src, dest) \
    *code++ = 0x29; \
    *code++ = MODRM(0x3, src, dest)

#define JUMP_EQ(mcode, label) jumpEqual(mcode, label)
#define JUMP_NOTEQ(mcode, label) jumpNotEqual(mcode, label)
#define GOTO(mcode, label) jumpTo(mcode, label)

/* reg_a AND reg_b, set flags */
#define TEST_REG_REG(code, reg_a, reg_b) \
    *code++ = 0x85; \
    *code++ = MODRM(0x3, reg_a, reg_b)

/* SETcc opcodes */
#define SET_NEQ_REG(code, reg) \
    *code++ = 0x0F; \
    *code++ = 0x95; \
    *code++ = MODRM(0x3, 0, reg)
#define SET_EQ_REG(code, reg) \
    *code++ = 0x0F; \
    *code++ = 0x94; \
    *code++ = MODRM(0x3, 0, reg)
#define SET_LT_REG(code, reg) \
    *code++ = 0x0F; \
    *code++ = 0x9C; \
    *code++ = MODRM(0x3, 0, reg)
#define SET_LE_REG(code, reg) \
    *code++ = 0x0F; \
    *code++ = 0x9E; \
    *code++ = MODRM(0x3, 0, reg)
#define SET_GT_REG(code, reg) \
    *code++ = 0x0F; \
    *code++ = 0x9F; \
    *code++ = MODRM(0x3, 0, reg)
#define SET_GE_REG(code, reg) \
    *code++ = 0x0F; \
    *code++ = 0x9D; \
    *code++ = MODRM(0x3, 0, reg)

/* movl %reg, %reg */
#define MOV_REG_REG(code, src, dest) \
    *code++ = 0x89; \
    *code++ = MODRM(0x3, src, dest)
/* movl $imm32, %reg */
#define MOV_IMM32_REG(code, imm, dest) \
    *code++ = 0xB8 + dest; \
    IMM32(code, imm)
/* movl disp8(%reg), %reg */
#define MOV_DISP8DREG_REG(code, disp, src, dest) \
    *code++ = 0x8B; \
    *code++ = MODRM(0x1, dest, src); \
    *code++ = disp
/* movl disp32(%reg), %reg */
#define MOV_DISP32DREG_REG(code, disp, src, dest) \
    *code++ = 0x8B; \
    *code++ = MODRM(0x2, dest, src); \
    IMM32(code, disp)
/* movl %reg, disp8(%reg) */
#define MOV_REG_DISP8DREG(code, src, disp, dest) \
    *code++ = 0x89; \
    *code++ = MODRM(0x1, src, dest); \
    *code++ = disp
/* movl (%reg), %reg */
#define MOV_DREG_REG(code, src, dest) \
    *code++ = 0x8B; \
    *code++ = MODRM(0x0, dest, src)
/* movl %reg, (%reg) */
/* XXX :) There was a bug here because I used
 * MODRM(0, dest, src) instead of MODRM(0, src, dest) */
#define MOV_REG_DREG(code, src, dest) \
    *code++ = 0x89; \
    *code++ = MODRM(0, src, dest)

#define CMOVLT_REG_REG(code, src, dest) \
    *code++ = 0x0F; \
    *code++ = 0x4C; \
    *code++ = MODRM(0x3, src, dest)


/* shr %cl, %reg */
#define SHR_CL_REG(code, reg) \
    *code++ = 0xD3; \
    *code++ = MODRM(0x3, 5, reg)
/* shr %cl, %(reg) */
#define SHR_CL_DREG(code, reg) \
    *code++ = 0xD3; \
    *code++ = MODRM(0, 5, reg)

#define LEAVE(code) *code++ = 0xC9
#define RETN(code) *code++ = 0xC3 /* Near return. */

#endif /* TCLJIT_X86_MCODE_H */
