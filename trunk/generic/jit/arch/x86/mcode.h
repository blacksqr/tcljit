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

#define MOV_REG_REG(code, src, dest) \
    *code++ = 0x89; \
    *code++ = MODRM(0x3, src, dest)
#define MOV_MEM_REG(code, mem, dest) \
    *code++ = 0xC7; \
    *code++ = MODRM(0x3, 0, dest); \
    IMM32(code, mem)

#define LEAVE(code) *code++ = 0xC9
#define RETN(code) *code++ = 0xC3 /* Near return. */

#endif /* TCLJIT_X86_MCODE_H */
