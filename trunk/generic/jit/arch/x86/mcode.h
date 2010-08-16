#ifndef TCLJIT_X86_MCODE_H
#define TCLJIT_X86_MCODE_H

typedef enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI } regs;

int allocReg(void *);

/* Auxiliary macros. */
#define IMM32(code, v) \
    *code++ = v; *code++ = v >> 8; *code++ = v >> 16; *code++ = v >> 24

#define MODRM(mod, reg, rm) (mod << 6) + (reg << 3) + rm

#define PROLOGUE(code) PUSH_REG(code, EBP); MOV_REG_REG(code, ESP, EBP)
#define EPILOGUE(code) LEAVE(code); RETN(code)


/* Instructions. */
#define NOP(code) *code++ = 0x90

#define INC_REG(code, reg) *code++ = 0x40 + reg
#define DEC_REG(code, reg) *code++ = 0x48 + reg

#define PUSH_REG(code, reg) *code++ = 0x50 + reg
#define POP_REG(code, reg) *code++ = 0x58 + reg

#define ADD_IMM8_REG(code, imm, reg) \
    *code++ = 0x83; \
    *code++ = MODRM(0x3, 0, reg); \
    *code++ = imm

/* movl %reg, %reg */
#define MOV_REG_REG(code, src, dest) \
    *code++ = 0x89; \
    *code++ = MODRM(0x3, src, dest)
/* movl $imm32, %reg */
#define MOV_IMM32_REG(code, imm32, dest) \
    *code++ = 0xB8 + dest; \
    IMM32(code, imm32)
/* movl disp8(%reg), %reg */
#define MOV_DISP8DREG_REG(code, disp, src, dest) \
    *code++ = 0x8b; \
    *code++ = MODRM(0x1, dest, src); \
    *code++ = disp
/* movl (%reg), %reg */
#define MOV_DREG_REG(code, src, dest) \
    *code++ = 0x8b; \
    *code++ = MODRM(0x0, dest, src)

/* XXX Using prefix D to stand for Dereference.
 * So, DREG means Deference at memory contained in Register. */

#define LEAVE(code) *code++ = 0xC9
#define RETN(code) *code++ = 0xC3 /* Near return. */

#endif /* TCLJIT_X86_MCODE_H */
