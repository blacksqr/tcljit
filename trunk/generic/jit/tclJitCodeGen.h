/*
 * Created:  20/07/2010 23:29:19
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#ifndef TCLJIT_CODEGEN_H
#define TCLJIT_CODEGEN_H

#include "tclJitCompile.h"

struct MCode {
    unsigned char *mcode, *codeEnd;
    int limit, used;
};

typedef struct MCode MCode;

unsigned char *JIT_CodeGen(struct BasicBlock *, int);

#endif /* TCLJIT_CODEGEN_H */
