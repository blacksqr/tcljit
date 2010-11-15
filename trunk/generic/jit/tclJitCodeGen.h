#ifndef TCLJIT_CODEGEN_H
#define TCLJIT_CODEGEN_H

#include "tclJitCompile.h"

struct MCode {
    unsigned char *mcode, *codeEnd;
    int limit, used;
};

typedef struct MCode MCode;

unsigned char *JIT_CodeGen(struct BasicBlock *, int, int);

#endif /* TCLJIT_CODEGEN_H */
