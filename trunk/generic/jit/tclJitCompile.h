/*
 * Created:  09/07/2010 23:52:45
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#ifndef TCLJIT_COMPILE_H
#define TCLJIT_COMPILE_H

#include "tclCompile.h"

typedef struct Value *Value;

struct Quadruple {
    Value dest;
    unsigned char instruction;
    Value src_a, src_b;
    struct Quadruple *next;
};

struct BasicBlock {
    int exitcount;
    struct Quadruple *quads, *lastquad;
    struct BasicBlock **exit;
};

struct Value {
    enum { jitvalue_tcl, jitvalue_int, jitreg } type;
    union {
        Tcl_Obj *obj;
        int integer;
        int regnum;
    } content;
};


int JIT_Compile(Tcl_Obj *, Tcl_Interp *, ByteCode *);

#endif /* TCLJIT_COMPILE_H */
