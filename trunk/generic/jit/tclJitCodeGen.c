#include <assert.h>
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

#define VREG_OFFSET(value) (value->content.vreg.offset)

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

void
pagenowrite(void *page, size_t len)
{
#ifdef _WIN32
    DWORD oldProtect;
    if (VirtualProtect(page, len, PAGE_EXECUTE_READ, &oldProtect) == 0) {
        perror("VirtualProtect");
        exit(1);
    }
#else
    if (mprotect(page, len, PROT_READ | PROT_EXEC) < 0) {
        perror("mprotect");
        exit(1);
    }
#endif
}


unsigned char *
JIT_CodeGen(struct BasicBlock *CFG, int bbcount, int stack_reserve)
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
    PROLOGUE(code.codeEnd, stack_reserve);

    /* Assuming that node 0 in the CFG is the source node. */
    //dfs(CFG, 0, visited, &code);
    dfs(CFG, bbcount, visited, &code);

    LABEL(&(code), "LEAVE");
    EPILOGUE(code.codeEnd, stack_reserve);
    /* Note: You may try not generating PROLOGUE and EPILOGUE but then you
     * will need to uncomment the next line. */
    /*RETN(code.codeEnd);*/

    LABEL(&(code), "LINTERP");
    int result = JIT_RESULT_INTERPRET;
    MOV_IMM32_REG(code.codeEnd, result, EAX);
    EPILOGUE(code.codeEnd, stack_reserve);

    /*
    int padding = ((code.codeEnd - code.mcode - 1) % 4) - 1;
    while (padding > 0) {
        NOP(code.codeEnd);
        padding--;
    }*/

    finishLabelTable();

#if DEBUGGING
    int i;
    for (i = 0; i < (code.codeEnd - code.mcode); i++) {
        printf("0x%X, ", code.mcode[i]);
    }
    printf("\n");
    printf(">>> %d <<<\n", (code.codeEnd - code.mcode));
#endif

    pagenowrite(code.mcode, code.limit);
    return code.mcode;

    /* XXX At some point either VirtualFree(code.mcode, 0, MEM_RELEASE)
     * or munmap(code.mcode, code.limit) needs to be called. */
}

static void
dfs(struct BasicBlock *CFG, int visiting, int visited[], MCode *code)
{
    int i, to_visit;
    char label[22];
    struct BasicBlock *block = &(CFG[visiting]);

    visited[visiting] = 1;

    DEBUG("##CG Visiting %d\n", block->id);
    snprintf(label, sizeof(label), "BB%d", block->id);
    LABEL(code, label);
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
    //long int offset;
    struct Quadruple *ptr;

    if (!quads || !(quads->next)) {
        /* XXX The special basic block may be empty, others shouldn't be. */
        //abort();
        return;
    }


    for (ptr = quads->next; ptr; ptr = ptr->next) {
        switch (ptr->instruction) {
        /* XXX Check for used and allocated size code.mcode before
         * advancing. */

        case JIT_INST_NOP:
            /* Test: */
            //NOP(code->codeEnd);
            break;

        case JIT_INST_SAVE: {
            //char *label = uniqueLabel();
            DEBUG("I have to save something %d.\n",
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)));

            //NOP(code->codeEnd);
            //NOP(code->codeEnd);

            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)), EBP, EDX);

            /* XXX Testing: EDX contains a number. */
            PUSH_REG(code->codeEnd, EDX); /* the number. */
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_NewLongObj, EAX);
            CALL_ABSOLUTE_REG(code->codeEnd, EAX); /* EAX contains an
                                                      new object. */

	    PUSH_REG(code->codeEnd, EAX); /* Saved Tcl_Obj pointer. */
	    PUSH_DISP8DREG(code->codeEnd, 8, EBP); /* Pointer to Interp */
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_SetObjResult, ECX);
            CALL_ABSOLUTE_REG(code->codeEnd, ECX);

            /* "Remove" 8(%ebp) and EAX from stack. */
            ADD_IMM8_REG(code->codeEnd, 8, ESP);

#if 0
            POP_REG(code->codeEnd, EDX); /* Pointer to Tcl_Obj. */
            /* XXX String update should be moved to where it is needed
             * (like inside the JIT_INCR) */
            /* Update the string representation. */
            POP_REG(code->codeEnd, EDX); /* Pointer to Tcl_Obj. */
            MOV_REG_REG(code->codeEnd, EDX, ECX);
            offset = offsetof(Tcl_Obj, typePtr);
            MOV_DISP8DREG_REG(code->codeEnd, offset, ECX, ECX);
            /* typePtr might be NULL, checking now. */
            CMP_IMM32_REG(code->codeEnd, 0, ECX);
            JUMP_NOTEQ(code, label);
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)&tclStringType, ECX);

            LABEL(code, label);
            free(label);
            offset = offsetof(Tcl_ObjType, updateStringProc);
            MOV_DISP8DREG_REG(code->codeEnd, offset, ECX, ECX);
            /* ECX now contains the address of the updateStringProc for int. */
            PUSH_REG(code->codeEnd, EDX); /* Saved Tcl_Obj pointer. */
            CALL_ABSOLUTE_REG(code->codeEnd, ECX);
            ADD_IMM8_REG(code->codeEnd, 4, ESP);
#endif

            regn = EAX;
            XOR_REG_REG(code->codeEnd, regn, regn); /* TCL_OK == 0 */
            GOTO(code, "LEAVE");
            break;
        }

        case JIT_INST_JTRUE:
        case JIT_INST_JFALSE:
            DEBUG("INST_JCOND HERE! %d %d\n", ptr->src_a->flags,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)));
            assert(ptr->src_a->type == jitreg);

            regn = EDX;
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, regn);

            /* XXX Testing: src_a is a number already: 0 or 1 */
            int test = 0;
            CMP_IMM32_REG(code->codeEnd, test, regn);

#if 0
            PUSH_REG(code->codeEnd, EBX);

            //SUB_IMM8_REG(code->codeEnd, 4, ESP);
            //LEA_DREG_REG(code->codeEnd, ESP, EBX);
            LEA_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, EBX);
            COPY_PARAM_REG(code->codeEnd, 0, ECX); /* Interp at ECX */

            PUSH_REG(code->codeEnd, EBX);
            PUSH_REG(code->codeEnd, regn);
            PUSH_REG(code->codeEnd, ECX);
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_GetBooleanFromObj, EAX);
            CALL_ABSOLUTE_REG(code->codeEnd, EAX);

            ADD_IMM8_REG(code->codeEnd, 8, ESP);

            /* Check for error from Tcl_GetBooleanFromObj */
            //CMP_IMM32_REG(code->codeEnd, 0, EAX);
            //JUMP_NOTEQ(code, "INTERP"); /* XXX */

            /* Check for True/False from Tcl_GetBooleanFromObj */
            POP_REG(code->codeEnd, EBX);
            MOV_DREG_REG(code->codeEnd, EBX, EBX);
            CMP_IMM32_REG(code->codeEnd, 0, EBX);

            POP_REG(code->codeEnd, EBX); /* Restore EBX */
#endif

            char label[22];
            snprintf(label, sizeof(label), "BB%d", ptr->dest->content.integer);
            if (ptr->instruction == JIT_INST_JFALSE) {
                DEBUG("Jump to label '%s' if it is false..\n", label);
                JUMP_EQ(code, label);
            } else {
                DEBUG("Jump to label '%s' if it is true..\n", label);
                JUMP_NOTEQ(code, label);
            }

            break;

        case JIT_INST_GOTO: {
            char label[22];
            snprintf(label, sizeof(label), "BB%d", ptr->dest->content.integer);
            GOTO(code, label);
            DEBUG("INST_GOTO! %s\n", label);
            break;
        }

        case INST_NEQ:
        case INST_EQ:
        case INST_GT:
        case INST_GE:
        case INST_LE:
        case INST_LT: {
            DEBUG("x INST_LOWER|INST_GREATER|INST_EQ y\n");

            int reg_a = ECX, reg_b = EDX;

            /* Load src_a and src_b from stack. */
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, reg_a);
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)),
                    EBP, reg_b);

            /* XXX Testing: reg_a and reg_b are numbers. */
            XOR_REG_REG(code->codeEnd, EAX, EAX);
            CMP_REG_REG(code->codeEnd, reg_b, reg_a);

            if (ptr->instruction == INST_GT) {
                SET_GT_REG(code->codeEnd, EAX);
            } else if (ptr->instruction == INST_GE) {
                SET_GE_REG(code->codeEnd, EAX);
            } else if (ptr->instruction == INST_LT) {
                SET_LT_REG(code->codeEnd, EAX);
            } else if (ptr->instruction == INST_EQ) {
                SET_EQ_REG(code->codeEnd, EAX);
            } else if (ptr->instruction == INST_NEQ) {
                SET_NEQ_REG(code->codeEnd, EAX);
            } else {
                SET_LE_REG(code->codeEnd, EAX);
            }

            /* Either 0 or 1 is in EAX now. */
            /* Save it. */
            DEBUG("       Saving at %d\n",
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)));
            MOV_REG_DISP8DREG(code->codeEnd, EAX, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

#if 0
            /* Go to their supposed integer value. */
            offset = offsetof(Tcl_Obj, internalRep.longValue);
            ADD_IMM8_REG(code->codeEnd, offset, reg_a);
            ADD_IMM8_REG(code->codeEnd, offset, reg_b);
            /* Get the integer value. */
            MOV_DREG_REG(code->codeEnd, reg_a, reg_a);
            MOV_DREG_REG(code->codeEnd, reg_b, reg_b);

            /* Compare the values. */
            XOR_REG_REG(code->codeEnd, EAX, EAX);
            CMP_REG_REG(code->codeEnd, reg_a, reg_b);
            if (ptr->instruction == INST_GT) {
                SET_GT_REG(code->codeEnd, EAX);
            } else if (ptr->instruction == INST_LT) {
                SET_LT_REG(code->codeEnd, EAX);
            } else {
                SET_LE_REG(code->codeEnd, EAX);
            }
            MUL_IMM32_REG_REG(code->codeEnd, sizeof(ptrdiff_t), EAX, EAX);
            MOV_REG_REG(code->codeEnd, EAX, ECX);

            /* Get either constant 0 or 1 from Interp->execEnvPtr->constants */
            regn = EAX;
            COPY_PARAM_REG(code->codeEnd, 0, regn);

            offset = offsetof(Interp, execEnvPtr);
            /* Warning: offset to execEnvPtr > 255 */
            MOV_DISP32DREG_REG(code->codeEnd, offset, regn, regn);

            offset = offsetof(ExecEnv, constants); /* +4 if x COND (>, >=) y */
            ADD_IMM8_REG(code->codeEnd, offset, regn);
            ADD_REG_REG(code->codeEnd, ECX, regn);
            MOV_DREG_REG(code->codeEnd, regn, regn);

            /* The proper constant is in regn now. */


            /* Save it. */
            printf("       Saving at %d\n",
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)));
            MOV_REG_DISP8DREG(code->codeEnd, regn, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
#endif
            break;
        }

        case INST_LOAD_SCALAR1: {
            DEBUG("!!!! Move param %d to pos %d\n",
                    ptr->src_a->offset,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)));
            //regn = load_localvar(code, ptr);
            load_localvar(code, ptr,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)));
            /* Save regn to the expected stack position. */
            //MOV_REG_DISP8DREG(code->codeEnd, regn, 
            //        INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
            break;
        }

                      /* XXX Checar questao de localvar */
        case JIT_INST_MOVE:
            DEBUG("INST_MOVE: %s <- %s (%d)\n", value_type_str[ptr->dest->type],
                    value_type_str[ptr->src_a->type],
                    ptr->src_a->flags & JIT_VALUE_PARAM);
            if (ptr->dest->type == jitreg) {
                if (ptr->src_a->type == jitreg && \
                        !(ptr->src_a->flags & JIT_VALUE_PARAM)) {
                    DEBUG("Move reg[%d] to reg[%d].\n",
                            VREG_OFFSET(ptr->src_a), VREG_OFFSET(ptr->dest));
                    regn = EDX;
                    MOV_DISP8DREG_REG(code->codeEnd,
                            INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                            EBP, regn);
                    MOV_REG_DISP8DREG(code->codeEnd, regn,
                            INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                            EBP);
                } else if (ptr->src_a->flags & JIT_VALUE_OBJARRAY) {
                    DEBUG("!!! Move objarray item %d to pos %d\n",
                            ptr->src_a->offset,
                            INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)));
                    /* XXX This is a test, ptr->src_a at offset 2 happens
                     * to hold an empty string "" in my tests. */
                    //if (ptr->src_a->offset != 2) {
                    load_tclobject(code, ptr->src_a->offset,
                            INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)));
                    //}
                } else if (ptr->src_a->flags & JIT_VALUE_LOCALVAR) {
                    abort();
                    DEBUG("Move (WRONG) param %d to pos %d\n",
                            ptr->src_a->offset,
                            INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)));
#if 0
                    regn = load_localvar(code, ptr);
                    /* Save regn to the expected stack position. */
                    MOV_REG_DISP8DREG(code->codeEnd, regn, 
                            INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
#endif
                } else if (ptr->src_a->type == jitvalue_int) {
                    MOV_IMM32_REG(code->codeEnd,
                            ptr->src_a->content.integer,
                            EAX);
                    MOV_REG_DISP8DREG(code->codeEnd, EAX,
                            INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                            EBP);
                } else {
                    /* XXX Unsupported. */
                    abort();
                }
                //NOP(code->codeEnd);

              //  if (ptr->dest->flags & JIT_VALUE_PARAM) {
                    /* Remove JIT_VALUE_PARAM flag from it */
              //      ptr->src_a->flags &= ~JIT_VALUE_PARAM;
              //  }
            } else {
                /* XXX Unsupported. */
                abort();
            }
            break;

        /* XXX Not working with the new model */
        case JIT_INST_INCR: {
            DEBUG("INST_INCR: %s += %d\n",
                    value_type_str[ptr->dest->type],
                    ptr->src_b->content.integer);
            if (ptr->src_b->type != jitvalue_int ||
                    ptr->src_a->type != jitreg ||
                    ptr->src_a != ptr->dest) {
                Tcl_Panic("Incorrectly encoded INCR instruction.");
            }

            int val = ptr->src_b->content.integer;

            regn = EAX;

	    //if (ptr->dest->flags & JIT_VALUE_LOCALVAR) {
	    if (ptr->src_a->flags & JIT_VALUE_LOCALVAR) {
                MOV_DISP8DREG_REG(code->codeEnd,
                        INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                        EBP, regn);
                 
                /* XXX Testing: get the integer and store it. */
#if 0
                regn = load_localvar(code, ptr);
                /* Save regn to the expected stack position. */
                MOV_REG_DISP8DREG(code->codeEnd, regn, 
                        INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

                /* Point to the possible long value. */
		offset = offsetof(Tcl_Obj, internalRep.longValue);
		ADD_IMM8_REG(code->codeEnd, offset, regn);
#endif
	    } else {
		Tcl_Panic("dur dur.");
	    }

            if (val == 1) {
                //INC_DREG(code->codeEnd, regn);
                INC_REG(code->codeEnd, regn); // XXX test
            } else if (val == -1) {
                //DEC_DREG(code->codeEnd, regn);
                DEC_REG(code->codeEnd, regn); // XXX Test
            } else {
                /* XXX */
                Tcl_Panic("Should have been a JIT_INST_ADD.");
            }

            MOV_REG_DISP8DREG(code->codeEnd, regn, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
            break;
        }

        /* XXX Adjust for the new model */
        case INST_BITXOR: {
            DEBUG("INST_BITXOR: %s[%d] = %s[%d] ^ %s[%d]\n",
                    value_type_str[ptr->dest->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                    value_type_str[ptr->src_a->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    value_type_str[ptr->src_b->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)));

            assert(ptr->dest->type == jitreg);
            //assert(ptr->dest->content.vreg.type == TCL_NUMBER_LONG);
            /* Assuming both operands are integers. */
            int reg_a = EDX, reg_b = ECX;

            /* Bring operands (Tcl_Obj(ects) *) from stack to registers. */
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, reg_a);
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)),
                    EBP, reg_b);

            XOR_REG_REG(code->codeEnd, reg_b, reg_a);
            MOV_REG_DISP8DREG(code->codeEnd, reg_a, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
#if 0
            PUSH_REG(code->codeEnd, reg_b);
            /* Duplicate src_a object. */
            PUSH_REG(code->codeEnd, reg_a);
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_DuplicateObj, EAX);
            CALL_ABSOLUTE_REG(code->codeEnd, EAX);
            /* EAX contains the duplicated object now (CALL return). */
            ADD_IMM8_REG(code->codeEnd, 4, ESP);
            POP_REG(code->codeEnd, reg_b);
            /* Increment its refcount. */
            INC_DREG(code->codeEnd, EAX);
            /* refCount is the first field in Tcl_Obj,
             * no need for displacement. */

            /* Copy EAX to the expected stack pos. */
            MOV_REG_DISP8DREG(code->codeEnd, EAX, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

            /* Point to their integer values. */
            offset = offsetof(Tcl_Obj, internalRep.longValue);
            ADD_IMM8_REG(code->codeEnd, offset, EAX);
            ADD_IMM8_REG(code->codeEnd, offset, reg_b);

            /* reg_a XOR reg_b. */
            MOV_DREG_REG(code->codeEnd, reg_b, reg_b);
            XOR_REG_DREG(code->codeEnd, reg_b, EAX);
#endif
            break;
        }

        /* XXX Adjust for the new model */
        case INST_RSHIFT: {
            DEBUG("INST_RSHIFT: %s[%d] = %s[%d] >> %s[%d]\n",
                    value_type_str[ptr->dest->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                    value_type_str[ptr->src_a->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    value_type_str[ptr->src_b->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)));

            assert(ptr->dest->type == jitreg);
            //assert(ptr->dest->content.vreg.type == TCL_NUMBER_LONG);
            /* Assuming both operands are integers. */
            int reg_a = EAX, reg_b = ECX;

            /* Bring operands (Tcl_Obj(ects) *) from stack to registers. */
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, reg_a);
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)),
                    EBP, reg_b);

            /* XXX Testing: reg_a and reg_b already got integer
             * values. */
            SHR_CL_REG(code->codeEnd, reg_a);
            MOV_REG_DISP8DREG(code->codeEnd, reg_a, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

#if 0
            PUSH_REG(code->codeEnd, reg_b);
            /* Duplicate src_a object. */
            PUSH_REG(code->codeEnd, reg_a);
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_DuplicateObj, EAX);
            CALL_ABSOLUTE_REG(code->codeEnd, EAX);
            /* EAX contains the duplicated object now (CALL return). */
            ADD_IMM8_REG(code->codeEnd, 4, ESP);
            POP_REG(code->codeEnd, reg_b);
            /* Increment its refcount. */
            INC_DREG(code->codeEnd, EAX);
            /* refCount is the first field in Tcl_Obj,
             * no need for displacement. */

            /* Copy EAX to the expected stack pos. */
            MOV_REG_DISP8DREG(code->codeEnd, EAX, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

            /* Point to their integer values. */
            offset = offsetof(Tcl_Obj, internalRep.longValue);
            ADD_IMM8_REG(code->codeEnd, offset, EAX);
            ADD_IMM8_REG(code->codeEnd, offset, reg_b);

            /* Right shift by reg_b. */
            MOV_DREG_REG(code->codeEnd, reg_b, reg_b);
            SHR_CL_DREG(code->codeEnd, EAX);
#endif

            break;
        }

        case INST_MULT: {
            DEBUG("INST_MULT: %s[%d] = %s[%d] * %s[%d]\n",
                    value_type_str[ptr->dest->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                    value_type_str[ptr->src_a->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    value_type_str[ptr->src_b->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)));

            assert(ptr->dest->type == jitreg);
            /* Assuming both operands are integers. */
            int reg_a = EAX, reg_b = EDX;

            /* Bring operands (Tcl_Obj(ects) *) from stack to registers. */
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, reg_a);
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)),
                    EBP, reg_b);

            /* XXX Testing: reg_a and reg_b already got integer
             * values. */
            MUL_REG(code->codeEnd, reg_b);
            MOV_REG_DISP8DREG(code->codeEnd, reg_a, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
#if 0
            PUSH_REG(code->codeEnd, reg_b);
            /* Duplicate src_a object. */
            PUSH_REG(code->codeEnd, reg_a);
            MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_DuplicateObj, EAX);
            CALL_ABSOLUTE_REG(code->codeEnd, EAX);
            /* EAX contains the duplicated object now (CALL return). */
            ADD_IMM8_REG(code->codeEnd, 4, ESP);
            POP_REG(code->codeEnd, reg_b);
            /* Increment its refcount. */
            INC_DREG(code->codeEnd, EAX);
            /* refCount is the first field in Tcl_Obj,
             * no need for displacement. */

            /* Copy EAX to the expected stack pos. */
            MOV_REG_DISP8DREG(code->codeEnd, EAX, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

            /* Point to their integer values. */
            offset = offsetof(Tcl_Obj, internalRep.longValue);
            ADD_IMM8_REG(code->codeEnd, offset, EAX);
            ADD_IMM8_REG(code->codeEnd, offset, reg_b);

            /* reg_a * reg_b. */
            MOV_DREG_REG(code->codeEnd, EAX, reg_a);
            MOV_DREG_REG(code->codeEnd, reg_b, reg_b);
            MUL_REG_REG(code->codeEnd, reg_b, reg_a);

            MOV_REG_DREG(code->codeEnd, reg_a, EAX);
#endif

            break;
        }

        case INST_LNOT: {
            DEBUG("INST_NOT: %s[%d] = !%s[%d]\n",
                    value_type_str[ptr->dest->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                    value_type_str[ptr->src_a->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)));

            int reg_a = ECX;

            /* Bring number from stack to register. */
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, reg_a);

            /* Store either 1 or 0. */
            XOR_REG_REG(code->codeEnd, EAX, EAX);
            CMP_IMM8_REG(code->codeEnd, 0, reg_a);
            SET_EQ_REG(code->codeEnd, EAX);

            /* Save result. */
            MOV_REG_DISP8DREG(code->codeEnd, EAX, 
                INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
            break;
        }

        case INST_DIV:
        case INST_MOD:
            DEBUG("INST_MOD|DIV: %s[%d] = %s[%d] %%|/ %s[%d]\n",
                    value_type_str[ptr->dest->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                    value_type_str[ptr->src_a->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    value_type_str[ptr->src_b->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)));
            assert(ptr->dest->type == jitreg);
            assert(ptr->dest->content.vreg.type == TCL_NUMBER_LONG);

            /* Assuming both operands are integers. */
            int reg_a = EAX, reg_b = ECX;

            /* Bring operands (Tcl_Obj(ects) *) from stack to registers. */
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    EBP, reg_a);
            MOV_DISP8DREG_REG(code->codeEnd,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)),
                    EBP, reg_b);

            /* Perform modulus and divison. */
            XOR_REG_REG(code->codeEnd, EDX, EDX);
            DIV_REG(code->codeEnd, reg_b);
            
            /* EDX contains the remainder, EAX the quocient. */

            if (ptr->instruction == INST_MOD) {
                MOV_REG_DISP8DREG(code->codeEnd, EDX, 
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
            } else {
                MOV_REG_DISP8DREG(code->codeEnd, EAX,
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);
            }
            break;

        case INST_SUB:
        case JIT_INST_ADD: /* or INST_ADD */
            DEBUG("INST_ADD/SUB: %s[%d] = %s[%d] +/- %s[%d]\n",
                    value_type_str[ptr->dest->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)),
                    value_type_str[ptr->src_a->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                    value_type_str[ptr->src_b->type],
                    INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)));
            assert(ptr->dest->type == jitreg);
            assert(ptr->dest->content.vreg.type != -1);

            switch (ptr->dest->content.vreg.type) {
            case TCL_NUMBER_LONG: {
                /* Assuming both operands are integers. */
                int reg_a = EDX, reg_b = ECX;

                /* Bring operands (Tcl_Obj(ects) *) from stack to registers. */
                MOV_DISP8DREG_REG(code->codeEnd,
                        INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_a)),
                        EBP, reg_a);
                MOV_DISP8DREG_REG(code->codeEnd,
                        INDEX_TO_STACKADDR(VREG_OFFSET(ptr->src_b)),
                        EBP, reg_b);

                /* XXX Testing: reg_a and reg_b already got integer
                 * values. */
                if (ptr->instruction == INST_ADD) {
                    ADD_REG_REG(code->codeEnd, reg_b, reg_a);
                } else {
                    SUB_REG_REG(code->codeEnd, reg_b, reg_a);
                }
                MOV_REG_DISP8DREG(code->codeEnd, reg_a, 
                        INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

#if 0
                PUSH_REG(code->codeEnd, reg_b);
                /* Duplicate src_a object. */
	        PUSH_REG(code->codeEnd, reg_a);
                MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_DuplicateObj, EAX);
                CALL_ABSOLUTE_REG(code->codeEnd, EAX);
                /* EAX contains the duplicated object now (CALL return). */
                ADD_IMM8_REG(code->codeEnd, 4, ESP);
                POP_REG(code->codeEnd, reg_b);
                /* Increment its refcount. */
                INC_DREG(code->codeEnd, EAX);
                /* refCount is the first field in Tcl_Obj,
                 * no need for displacement. */

                /* Copy EAX to the expected stack pos. */
                MOV_REG_DISP8DREG(code->codeEnd, EAX, 
                        INDEX_TO_STACKADDR(VREG_OFFSET(ptr->dest)), EBP);

                /* Point to their integer values. */
		offset = offsetof(Tcl_Obj, internalRep.longValue);
		ADD_IMM8_REG(code->codeEnd, offset, EAX);
		ADD_IMM8_REG(code->codeEnd, offset, reg_b);

                /* Add or Sub. */
                MOV_DREG_REG(code->codeEnd, reg_b, reg_b);
                if (ptr->instruction == INST_ADD) {
                    ADD_REG_DREG(code->codeEnd, reg_b, EAX);
                } else {
                    SUB_REG_DREG(code->codeEnd, reg_b, EAX);
                }
#endif

                break;
            }

            default:
                abort();
                break;
            }

            break; /* end case JIT_INST_ADD */

        default:
            DEBUG("ERROR: Instruction '%d' not supported!\n", ptr->instruction);
            abort();
            break;
        }
    }
}
