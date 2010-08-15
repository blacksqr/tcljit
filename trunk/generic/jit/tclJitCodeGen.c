/*
 * Created:  20/07/2010 23:29:56
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* XXX Check for Windows. */
#include "tclJitCompile.h"
#include "tclJitCodeGen.h"
#include "tclJitInsts.h"
#include "arch/arch.h"

//#if DEBUGGING
//#include "tclJitOutput.h"
static const char *value_type_str[] = {
        /* jitvalue_tcl = "Tcl", jitvalue_int = "int", jitreg = "Reg",
         *     jitvalue_long = "long", jitvalue_double = "double"*/
        "Tcl", "int", "Reg", "long", "double"
};
//#endif

static void dfs(struct BasicBlock *, int, int *, MCode *);
static void codegen(struct Quadruple *, MCode *);


unsigned char *
JIT_CodeGen(struct BasicBlock *CFG, int bbcount)
{
    MCode code;
    unsigned char *codeStart;
    int visited[bbcount];

    if (!bbcount) {
        /* Shouldn't happen. No basic block to generate code for. */
	return NULL;
    }

    code.used = 0;
    code.limit = getpagesize(); /* XXX Adjust for Windows. */
    code.mcode = malloc(code.limit);
    codeStart = code.mcode;

    memset(visited, 0, sizeof(visited));

    /* XXX There could be a register allocation phase here. */

    PROLOGUE(code.mcode);

    /* Assuming that node 0 in the CFG is the source node. */
    dfs(CFG, 0, visited, &code);

    EPILOGUE(code.mcode);

    int i;
    for (i = 0; i < (code.mcode - codeStart); i++) {
        printf("0x%X ", code.mcode[i]);
    }
    printf("\n");
    printf(">>> %d <<<\n", (code.mcode - codeStart));

    return code.mcode; /* XXX retornar algo do mmap invés ? */
}

static void
dfs(struct BasicBlock *CFG, int visiting, int visited[], MCode *code)
{
    int i, to_visit;
    struct BasicBlock *block = &(CFG[visiting]);

    visited[visiting] = 1;

    DEBUG("##CG Visiting %d\n", block->id);
    /* Generate code for this block. */
    codegen(block->quads, code);

    //for (i = block->exitcount - 1; i >= 0; i--) {
    for (i = 0; i < block->exitcount; i++) {
        /* Go to the next basic block. */
        to_visit = block->exitblocks[i];
        if (!visited[to_visit]) {
            dfs(CFG, to_visit, visited, code);
        }
    }
}


static void
codegen(struct Quadruple *quads, MCode *code)
{
    unsigned char *mcode = code->mcode;
    struct Quadruple *ptr;

    if (!quads || !(quads->next)) {
        /* XXX Shouldn't be empty. No quadruples in a basic block. */
        abort();
        return;
    }

    for (ptr = quads->next; ptr; ptr = ptr->next) {
        switch (ptr->instruction) {

        case JIT_INST_MOVE:
            DEBUG("INST_MOVE: %s <- %s\n", value_type_str[ptr->dest->type],
                    value_type_str[ptr->src_a->type]);
            /* XXX Possibly expand code.mcode before advancing. */
            if (ptr->dest->type == jitreg) {
                if (ptr->src_a->type == jitreg) {
                    MOV_REG_REG(mcode, allocReg(ptr->src_a),
                            allocReg(ptr->dest));
                } else if (ptr->src_a->type == jitvalue_tcl) {
                    MOV_MEM_REG(mcode, (ptrdiff_t)ptr->src_a->content.obj,
                            allocReg(ptr->dest));
                } else {
                    /* XXX Unsupported. */
                }
            } else {
                /* XXX Unsupported. */
            }
            break;
        }
    }
}
