/*
 * Created:  09/07/2010 23:52:45
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#ifndef TCLJIT_COMPILE_H
#define TCLJIT_COMPILE_H

#include "tclCompile.h"

#define DEBUGGING 1 /*  XXX Maybe use something from Tcl instead. */
#define DEBUG(str, vaargs...)                           \
    do {                                                \
        if (DEBUGGING) fprintf(stderr, str, ##vaargs);  \
    } while (0)

typedef struct Value *Value;

struct Quadruple {
    Value dest;
    unsigned char instruction;
    Value src_a, src_b;
    struct Quadruple *next;
};

struct BasicBlock {
    int id;
    int exitcount;
    struct Quadruple *quads, *lastquad;
    /*struct BasicBlock **exit;*/
    int *exitblocks;
};

struct Value {
    enum { jitvalue_tcl, jitvalue_int, jitreg, jitvalue_long,
	   jitvalue_double } type;

    /* Flags are used to indicate where the value can be found in
     * runtime. See below for the available flags. */
    int flags;
    /* Offset is used when the value may be in an array, indicating
     * its position. */
    int offset;

    union {
        Tcl_Obj *obj;
        int integer;
        struct {
            int regnum;
            int type; /* XXX This is likely to change. */
        } vreg;
        long lval;
        double dval;
    } content;
};

/* The existent flags are NOT meant to be ORed. */
#define JIT_VALUE_LOCALVAR 0
#define JIT_VALUE_OBJARRAY 1


int JIT_Compile(Tcl_Obj *, Tcl_Interp *, ByteCode *);

#endif /* TCLJIT_COMPILE_H */

/* Emacs configuration.
 *
 * Local Variables:
 *   mode: c
 *   c-basic-offset: 4
 *   fill-column: 78
 * End:
 */
