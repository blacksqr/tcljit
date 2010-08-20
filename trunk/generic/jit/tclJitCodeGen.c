/*
 * Created:  20/07/2010 23:29:56
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#ifdef _WIN32
    /* XXX Sections under _WIN32 weren't tested. */
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/mman.h>
#endif
#include "tclInt.h"
#include "tclJitCompile.h"
#include "tclJitCodeGen.h"
#include "tclJitInsts.h"
#include "tclJitExec.h"
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

#ifdef _WIN32
DWORD
#else
int
#endif
pagesize(void)
{
#ifdef _WIN32
    DWORD pagesize;
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return getpagesize();
#endif
}

void *
newpage(void *size)
{
    void *page;

#ifdef _WIN32
    page = VirtualAlloc(NULL, *((DWORD *)size), MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE);
    if (!page) {
        perror("VirtualAlloc");
        exit(1);
    }
#else
    page = mmap(NULL, *((size_t *)size), PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_ANON | MAP_PRIVATE, 0, 0);
    if (page == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
#endif

    return page;
}

unsigned char *
JIT_CodeGen(struct BasicBlock *CFG, int bbcount)
{
    MCode code;
    int visited[bbcount];

    if (!bbcount) {
        /* Shouldn't happen. No basic block to generate code for. */
	return NULL;
    }

    code.used = 0;
    code.limit = pagesize();
    code.mcode = newpage(&code.limit);
    code.codeEnd = code.mcode;

    memset(visited, 0, sizeof(visited));

    /* XXX There could be a register allocation phase here. */

    initLabelTable();
    //PROLOGUE(code.codeEnd);

    /* Assuming that node 0 in the CFG is the source node. */
    dfs(CFG, 0, visited, &code);

    //EPILOGUE(code.codeEnd);
    RETN(code.codeEnd);
    finishLabelTable();

    int i;
    for (i = 0; i < (code.codeEnd - code.mcode); i++) {
        printf("0x%X ", code.mcode[i]);
    }
    printf("\n");
    printf(">>> %d <<<\n", (code.codeEnd - code.mcode));

    return code.mcode;

    /* XXX At some point either VirtualFree(code.mcode, 0, MEM_RELEASE)
     * or munmap(code.mcode, code.limit) needs to be called. */
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
    int regn;
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
                    MOV_REG_REG(code->codeEnd,
                            allocReg(ptr->src_a),
                            allocReg(ptr->dest));
                } else if (ptr->src_a->type == jitvalue_tcl) {
                    MOV_IMM32_REG(code->codeEnd,
                            (ptrdiff_t)ptr->src_a->content.obj,
                            allocReg(ptr->dest));
                } else {
                    /* XXX Unsupported. */
                }
            } else {
                /* XXX Unsupported. */
            }
            break;

        case JIT_INST_INCR:
            DEBUG("INST_INCR: %s += %d\n",
                    value_type_str[ptr->dest->type],
                    ptr->src_b->content.integer);
            if (ptr->src_b->type != jitvalue_int ||
                    ptr->src_a->type != jitreg ||
                    ptr->src_a != ptr->dest) {
                Tcl_Panic("Incorrectly encoded instruction.");
            }

            long int offset;
            int val = ptr->src_b->content.integer;

	    /* XXX Artificial code (missing proper register allocation). */
	    regn = EAX; /* XXX allocReg(ptr->dest); */

	    if (ptr->dest->content.vreg.flags == JIT_VALUE_LOCALVAR) {
		/* Load local variable into regn. */

                COPY_PARAM_REG(code->codeEnd, 0, regn);
		/* The only param passed to the func is in EAX (regn) now. */

		/* regn is pointing at an Interp struct. */

		offset = offsetof(Interp, varFramePtr);
		MOV_DISP8DREG_REG(code->codeEnd, offset, regn, regn);
		/* regn is now pointing at an CallFrame struct. */

		offset = offsetof(CallFrame, compiledLocals);
		MOV_DISP8DREG_REG(code->codeEnd, offset, regn, regn);
		/* regn is now pointing at an array of Var structs. */

		if (ptr->dest->content.vreg.offset) {
		    ADD_IMM8_REG(code->codeEnd,
				 sizeof(Var) * ptr->dest->content.vreg.offset,
				 regn);
		    /* regn is now pointing to the correct element in the
		     * Var array. */
		}

		offset = offsetof(Var, value.objPtr);
		MOV_DISP8DREG_REG(code->codeEnd, offset, regn, regn);
		MOV_REG_REG(code->codeEnd, regn, EDX); /* XXX Copy of Tcl_Obj*/
		/* regn is now pointing to the expected Tcl_Obj. */

                /* Check for typePtr == &tclIntType */
                /*PUSH_REG(code->codeEnd, EDX);
                offset = offsetof(Tcl_Obj, typePtr);
                MOV_DISP8DREG_REG(code->codeEnd, offset, EDX, EDX);
                MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)&tclIntType, ECX);
                CMP_REG_REG(code->codeEnd, EDX, ECX);
                JUMP_EQ(code, "LINTERP");
                POP_REG(code->codeEnd, EDX);*/

		offset = offsetof(Tcl_Obj, internalRep.longValue);
		ADD_IMM8_REG(code->codeEnd, offset, regn);
                /* regn is now pointing to the possible long value. */
	    } else {
		Tcl_Panic("dur dur.");
	    }

            if (val == 1) {
                INC_DREG(code->codeEnd, regn);
            } else if (val == -1) {
                DEC_DREG(code->codeEnd, regn);
            } else {
                /* XXX */
                Tcl_Panic("Should have been a JIT_INST_ADD.");
            }

	    /* XXX Supondo, de mentira, que retornaremos agora então é
	     * necessário invocar Tcl_SetObjResult. */
	    PUSH_REG(code->codeEnd, EDX); /* Saved Tcl_Obj pointer. */
	    PUSH_DISP8REG(code->codeEnd, 8, EBP); /* Pointer to Interp */
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_SetObjResult, ECX);
            CALL_ABSOLUTE_REG(code->codeEnd, ECX);

            /* "Remove" 8(EBP) from stack. */
            ADD_IMM8_REG(code->codeEnd, 4, ESP);

            /* Update the string representation. */
            POP_REG(code->codeEnd, EDX); /* Pointer to Tcl_Obj. */
            MOV_REG_REG(code->codeEnd, EDX, ECX);
            offset = offsetof(Tcl_Obj, typePtr);
            MOV_DISP8DREG_REG(code->codeEnd, offset, ECX, ECX);
            offset = offsetof(Tcl_ObjType, updateStringProc);
            MOV_DISP8DREG_REG(code->codeEnd, offset, ECX, ECX);
            /* ECX now contains the address of the updateStringProc for int. */
            PUSH_REG(code->codeEnd, EDX); /* Saved Tcl_Obj pointer. */
            CALL_ABSOLUTE_REG(code->codeEnd, ECX);

            ADD_IMM8_REG(code->codeEnd, 4, ESP);
            /* XXX Trying to not depend on stack frame. */

            XOR_REG_REG(code->codeEnd, regn, regn); /* TCL_OK == 0 */
            //GOTO(code, "OK");
            /* Interpret the code instead. */
            //LABEL(code, "LINTERP");
            //int result = JIT_RESULT_INTERPRET;
            //MOV_IMM32_REG(code->codeEnd, result, EAX);

            //LABEL(code, "OK");
            break;

        case JIT_INST_ADD:
            DEBUG("INST_ADD: %s = %s + %s\n",
                    value_type_str[ptr->dest->type],
                    value_type_str[ptr->src_a->type],
                    value_type_str[ptr->src_b->type]);
            /* XXX Not done. */
            break;

        }
    }
}
