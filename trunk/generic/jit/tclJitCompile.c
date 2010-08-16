#include <stdio.h>
#include <stdlib.h> /* XXX malloc vai provavelmente precisar ser substituído */
#include <string.h>
#include "tclJitCompile.h"
#include "tclJitOutput.h"
#include "tclJitInsts.h"
#include "tclJitTypeCollect.h"
#include "tclJitCodeGen.h"

#define IS_JUMP_INST(op) (op == INST_JUMP1 || op == INST_JUMP4 ||	\
			  op == INST_JUMP_TRUE1 || op == INST_JUMP_TRUE4 || \
			  op == INST_JUMP_FALSE1 || op == INST_JUMP_FALSE4)

#define IS_JIT_CJUMP_INST(op) (op == JIT_INST_JTRUE || op == JIT_INST_JFALSE)

/* Using a temporary stack to convert the stack machine nature of the TVM
 * to a form of register machine. */
struct stack {
    void *v;
    struct stack *next;
};

struct stack *Stack;
//typedef void *localstack_t;
typedef struct Quadruple *localstack_t;

/* XXX No verification on these stack functions yet. */
struct stack *
stack_new()
{
    struct stack *s = malloc(sizeof(struct stack));
    s->next = NULL;
    return s;
}

void
stack_push(struct stack *s, localstack_t v)
{
    struct stack *n = malloc(sizeof(struct stack));
    n->v = v;
    n->next = s->next;
    s->next = n;
}

void
stack_set_top(struct stack *s, localstack_t v)
{
    s->next->v = v;
}

localstack_t
stack_top(struct stack *s)
{
    return s->next->v;
}

localstack_t
stack_belowtop(struct stack *s)
{
    return s->next->next->v;
}

localstack_t
stack_get_at(struct stack *s, int pos)
{
    struct stack *ptr = s->next;

    while (pos--) {
        ptr = ptr->next;
    }
    return ptr->v;
}

localstack_t
stack_pop(struct stack *s)
{
    struct stack *temp = s->next;
    localstack_t v = temp->v;

    s->next = s->next->next;
    free(temp);

    return v;
}


Value
new_tclvalue(Tcl_Obj *obj)
{
    Value val = malloc(sizeof(Value));

    val->type = jitvalue_tcl;
    val->content.obj = obj;
    return val;
}

Value
new_intvalue(int integer)
{
    Value val = malloc(sizeof(Value));

    val->type = jitvalue_int;
    val->content.integer = integer;
    return val;
}

Value
new_register(int reset, int offset, int flags)
{
    static int regnum = 0;
    if (reset) {
        regnum = 0;
        return NULL;
    }

    Value reg = malloc(sizeof(Value));

    regnum++;
    reg->type = jitreg;
    reg->content.vreg.regnum = regnum;
    reg->content.vreg.offset = offset; /* XXX explain */
    reg->content.vreg.flags = flags; /* XXX explain */
    reg->content.vreg.type = -1;
    return reg;
}

void
tclobj_to_long(Value v)
{
    if (v->type != jitreg) {
        perror("tclobj_to_long");
        return;
    }
    v->content.vreg.type = TCL_NUMBER_LONG;
    //v->content.lval = v->content.obj->internalRep.longValue; /* XXX */
}

/* XXX */
#define GET_INT(value) value->content.integer
#define REG_RESETCOUNT new_register(1, -1, -1)
#define REG_NEW new_register(0, -1, -1)
#define REG_NEW_EX(offset, flags) new_register(0, offset, flags)

struct ObjReg {
    //Var *var;
    //Tcl_Obj **obj;
    Tcl_Obj *obj;
    Value reg;
};

struct Quadruple *build_quad(ByteCode *, unsigned char *, int *, int, int[],
			     struct ObjReg *);
void freeblocks(struct BasicBlock *, int);


/* XXX Preciso coletar tipos durante execução (TclExecute) também. Nem tanto
 * para montar o CFG e a representação em SSA, mas para gerar código. */

/* Bytecodes obsoletos (que eu não vou me preocupar): INST_CALL_FUNC1,
 * INST_CALL_BUILTIN_FUNC1 */


int
JIT_Compile(Tcl_Obj *procName, Tcl_Interp *interp, ByteCode *code)
{
    int i, j, k;
    int runningcount, advance, quadcount;
    int leaders[code->numCodeBytes + 1], bc_to_bb[code->numCodeBytes + 1];
    int numblocks;
    unsigned char *pc, op;
    unsigned char *ncode;
    Var *compiledLocals;
    struct Quadruple *ptr, *quad;
    struct BasicBlock *blocks;
    struct ObjReg locals[((Interp *)interp)->varFramePtr->numCompiledLocals];

    Stack = stack_new();
    pc = code->codeStart;
    memset(leaders, 0, code->numCodeBytes * sizeof(int));

    REG_RESETCOUNT;
    compiledLocals = ((Interp *)interp)->varFramePtr->compiledLocals;
    for (i = 0; i < ((Interp *)interp)->varFramePtr->numCompiledLocals; i++) {
        if (compiledLocals[i].value.objPtr != NULL) {
            /* This corresponds to a formal parameter. */
            locals[i].obj = Tcl_DuplicateObj(compiledLocals[i].value.objPtr);
            //locals[i].obj = &(compiledLocals[i].value.objPtr);
        } else {
	    //printf("NULL at locals %d<<\n", i);
            locals[i].obj = NULL;
        }
        //locals[i].reg = REG_NEW;
        locals[i].reg = REG_NEW_EX(i, JIT_VALUE_LOCALVAR);
    }

    DEBUG("proc = %s\n", TclGetString(procName));
    DEBUG("numCodeBytes = %d, numSrcBytes = %d\n"
            "numLitObjects = %d, numCompiledLocals = %d\n",
            code->numCodeBytes, code->numSrcBytes,
            code->numLitObjects, i);
#ifdef DEBUGGING
    for (i = 0; i < code->numLitObjects; i++) {
        DEBUG(">>%s\n", Tcl_GetString(code->objArrayPtr[i]));
    }
#endif

    numblocks = 1;
    leaders[code->numCodeBytes] = code->numCodeBytes;
    //leaders[0] = 1;
    pc++;
    for (i = 1; i < code->numCodeBytes; i++) {
        op = *pc;
        if (IS_JUMP_INST(op)) {
            leaders[i + *(pc + 1)] = 1; /* XXX May take 4 bytes. */
            leaders[i + 2] = 1;
            pc++; i++;
        }
        pc++;
    }
    /* Instruction 0 is always a leader, so we skip it here. */
    for (j = i = code->numCodeBytes - 1; i > 0; i--) {
        if (leaders[i]) {
            numblocks++;
            leaders[j] = i;
            if (j > i) {
                leaders[i] = 0;
            }
            j--;
        }
    }

    /* Map bytecode position to block number. */
    //printf("Leaders (%d): ", numblocks);
    k = j;
    int bcn = 0; /* bcn stands for bytecode number. */
    for (i = 0; i < numblocks; i++, j++) {
        //printf("%d (%d), ", leaders[j], leaders[j + 1] - leaders[j]);
        for (bcn = leaders[j]; bcn < leaders[j] + leaders[j + 1] - 1; bcn++) {
            bc_to_bb[bcn] = i;
        }
    }
    j = k;
    //printf("\n");

    blocks = malloc(numblocks * sizeof(struct BasicBlock));
#ifdef DEBUGGING
    pc = code->codeStart;
    DEBUG("%d", *pc);
    pc++;
    for (i = 1; i < code->numCodeBytes; i++) {
        DEBUG(", %d", *pc);
        pc++;
    }
    DEBUG("\n");
#endif

    /* Build basic blocks. */
    pc = code->codeStart;
    runningcount = 0;
    for (i = 0; i < numblocks; j++, i++) {
        blocks[i].id = i;
        blocks[i].quads = calloc(1, sizeof(struct Quadruple));
        blocks[i].quads->next = NULL;
        blocks[i].exitcount = 0;
        ptr = blocks[i].quads;
        quadcount = leaders[j + 1] - leaders[j];
        for (k = 0; k < quadcount; k++) {
            advance = 0;
            quad = build_quad(code, pc, &advance, runningcount + k,
			      bc_to_bb, locals);
            k += (advance - 1);
            pc += advance;

            if (quad != NULL) {
                ptr->next = quad;
                ptr = ptr->next;
            } /* Otherwise this bytecode doesn't generate a quad. */
        }
        runningcount += k;
        blocks[i].lastquad = ptr;
        //printf("%d -> %d\n", leaders[j], leaders[j + 1] - 1);
        //printf("\n\n%d %p\n\n", i, ptr);
    }

    /* Add initial flow information to the blocks. */
    for (i = 0; i < numblocks; i++) {
        if (!IS_JUMP_INST(blocks[i].lastquad->instruction)) {
            if (i + 1 == numblocks) {
                /* This is the "exit" block. */
                DEBUG("exit block: %d\n", i);
            } else {
                DEBUG("exits %d: %d\n", i, i + 1);
                blocks[i].exitcount = 1;
                blocks[i].exitblocks = malloc(1 * sizeof(int));
                blocks[i].exitblocks[0] = i + 1;
            }
        } else {
            if (i + 1 == numblocks) {
                /* This is the "exit" block. */
                DEBUG("exit block: %d %d\n", i,
		      GET_INT(blocks[i].lastquad->src_a));
                blocks[i].exitcount = 1;
                blocks[i].exitblocks = malloc(1 * sizeof(int));
                blocks[i].exitblocks[0] = GET_INT(blocks[i].lastquad->dest);
            } else {
                DEBUG("exits %d: %d %d\n", i, i + 1,
		      GET_INT(blocks[i].lastquad->dest));
                blocks[i].exitcount = 2;
                blocks[i].exitblocks = malloc(2 * sizeof(int));
                blocks[i].exitblocks[0] = i + 1;
                blocks[i].exitblocks[1] = GET_INT(blocks[i].lastquad->dest);
                /*
                if (blocks[i].lastquad->instruction != JIT_INST_GOTO) {
                    blocks[i].exitcount = 2;
                    blocks[i].exitblocks = malloc(2 * sizeof(int));
                    blocks[i].exitblocks[0] = i + 1;
                    blocks[i].exitblocks[1] = GET_INT(blocks[i].lastquad->dest);
                } else {
                    blocks[i].exitcount = 1;
                    blocks[i].exitblocks = malloc(1 * sizeof(int));
                    blocks[i].exitblocks[0] = GET_INT(blocks[i].lastquad->dest);
                }
                */
            }
        }
    }

    JIT_bb_output(TclGetString(procName), blocks, numblocks);
    printf("\n-------------\n\n");

    ncode = JIT_CodeGen(blocks, numblocks);
    if (ncode == NULL) {
        perror("JIT_Compile");
        exit(1);
    }
    code->procPtr->jitproc.ncode = ncode;

    /* XXX Missing free for "locals". */
    freeblocks(blocks, numblocks);

    return 0;
}

struct Quadruple *
build_quad(ByteCode *code, unsigned char *pc, int *adv, int pos, int bc_to_bb[],
	   struct ObjReg *locals)
{
    struct Quadruple *quad = calloc(1, sizeof(struct Quadruple));

    quad->instruction = *pc;
    DEBUG("%d ", quad->instruction);

    /* XXX O que fazer com INST_DONE ? Pular e retornar próxima instrução ? */

    switch (quad->instruction) {

    case INST_PUSH1: /* 1 */
	DEBUG(", pushing %d, ", *(pc + 1));
	quad->instruction = JIT_INST_MOVE;
	quad->dest = REG_NEW;
	quad->src_a = new_tclvalue(code->objArrayPtr[*(pc + 1)]);
	stack_push(Stack, quad);//->dest);
	*adv = 2; /* 1 for push instruction plus 1 for the index */
	break;

    case INST_INVOKE_STK1: /* 6 */
	DEBUG(", invoking (argc = %d), ", *(pc + 1));
	quad->instruction = JIT_INST_CALL;
	/* src_a: register "holding" the objv address.
	 * src_b: parameters. */
	quad->src_a = stack_get_at(Stack, *(pc + 1) - 1)->dest;
	//quad->src_b = new_listvalue(Stack, *(pc + 1) - 1);
	*adv = 2;
	break;

    case INST_JUMP_FALSE1: /* 38 */
	DEBUG(", offset (jf %d %d), ", (char)*(pc + 1),
	      bc_to_bb[pos + (char)*(pc + 1)]);
	quad->instruction = JIT_INST_JFALSE;
	quad->src_a = stack_pop(Stack)->dest;
	quad->dest = new_intvalue(bc_to_bb[pos + (char)*(pc + 1)]);
	*adv = 2;
	break;

    case INST_JUMP_TRUE1: /* 36 */
	DEBUG(", offset (jt %d %d), ", (char)*(pc + 1),
	      bc_to_bb[pos + (char)*(pc + 1)]);
	quad->instruction = JIT_INST_JTRUE;
	quad->src_a = stack_pop(Stack)->dest;
	quad->dest = new_intvalue(bc_to_bb[pos + (char)*(pc + 1)]);
	*adv = 2;
	break;

    case INST_JUMP1: /* 34 */
	DEBUG(", jump (%d %d), ", (char)*(pc + 1),
	      bc_to_bb[pos + (char)*(pc + 1)]);
	quad->instruction = JIT_INST_GOTO;
	quad->dest = new_intvalue(bc_to_bb[pos + (char)*(pc + 1)]);
	*adv = 2;
	break;

    case INST_POP: /* 3 */
	DEBUG(", pop, ");
	stack_pop(Stack);
	free(quad);   /* Discard this quad. */
	quad = NULL;  /* And get the next opcode. */
	*adv = 1;
	break;

    case INST_TRY_CVT_TO_NUMERIC: { /* 64 */
	DEBUG(" (type %d), ", JIT_TYPE_GET(code->procPtr, pos));
	if (JIT_TYPE_RESOLVED(code->procPtr, pos)) {
	    Value reg = stack_top(Stack)->dest;
	    if (reg->type != jitreg) {
		perror("INST_TRY_CVT_TO_NUMERIC");
	    } else {
		reg->content.vreg.type = JIT_TYPE_GET(code->procPtr, pos);
	    }
	} else {
	    /* XXX */
	}
	free(quad);
	quad = NULL;
	*adv = 1;
	break;
    }

    case INST_LOAD_SCALAR1: { /* 10 */
	//DEBUG(", loadscalar (%d #%p %d %d#), ", *(pc + 1), local,
	//      TclIsVarLink(local), TclIsVarDirectReadable(local));
	DEBUG(", loadscalar (%d), ", *(pc + 1));

	//Var *local = locals[*(pc + 1)].var;
	//Tcl_Obj *local_obj = Tcl_DuplicateObj(*(locals[*(pc + 1)].obj));
	Tcl_Obj *local_obj = locals[*(pc + 1)].obj;

	quad->instruction = JIT_INST_MOVE;
	quad->dest = REG_NEW;

	/*while (TclIsVarLink(local)) {
	    printf("Following link..\n");
	    local = local->value.linkPtr;
	}
	if (TclIsVarDirectReadable(local)) {
	    printf(">>>> %p %p\n", local, local->value);
	    local_obj = local->value.objPtr;
	} else {
	    printf("LOADING SCALAR at %p %d %d \n", local,
		   TclIsVarScalar(local), TclIsVarUndefined(local));
	    local_obj = TclPtrGetVar(NULL, local, NULL,
				     NULL, NULL, 0, *(pc + 1));
	    printf(">>>> %p <<<<\n", local_obj);
	    }*/
	/*local_obj = local->value.objPtr;
	if (local_obj) {
	    //Tcl_IncrRefCount(local_obj);
	    Tcl_DuplicateObj(local_obj);
	    }*/
	quad->src_a = new_tclvalue(local_obj);

	stack_push(Stack, quad);
	*adv = 2;
	break;
    }

    case INST_STORE_SCALAR1: { /* 17 */
	/* XXX Simplified this opcode for now. */
	//DEBUG(", storescalar (%d flags: %d), ", *(pc + 1),
	//        locals[*(pc + 1)].var.flags);
	DEBUG(", storescalar (%d @ %p), ",
	      *(pc + 1), locals[*(pc + 1)].reg);

	struct Quadruple *top = stack_top(Stack);

	quad->instruction = JIT_INST_MOVE;
	quad->dest = locals[*(pc + 1)].reg;
	//locals[*(pc + 1)].var->value.objPtr = top->src_a->content.obj;
        /* XXX !! */
	//*(locals[*(pc + 1)].obj) = top->src_a->content.obj;
	locals[*(pc + 1)].obj = top->src_a->content.obj;
	quad->src_a = top->dest;
	//stack_set_top(Stack, top->dest);
	*adv = 2;
	break;
    }

    case INST_INCR_SCALAR1_IMM: /* 29 */
	DEBUG(", incr_imm (%d %d), ", *(pc + 1), *(pc + 2));
        /* XXX quad->src_a needs to be able to be converted to an integer,
         * could signal this using JIT_ResolveType here. */
	//quadr->dest = new_tclvalue(locals[*(pc + 1)].obj);//locals[*(pc + 1)].reg;
	quad->dest = locals[*(pc + 1)].reg;
	quad->src_a = locals[*(pc + 1)].reg;
	quad->src_b = new_intvalue(*(pc + 2));
        if (abs(quad->src_b->content.integer) == 1) {
            quad->instruction = JIT_INST_INCR;
        } else {
            quad->instruction = JIT_INST_ADD;
        }
	stack_push(Stack, quad);
	*adv = 3;
	break;

    case INST_LT: /* 47 */
	DEBUG(", top < belowTop, ");
	quad->src_b = stack_top(Stack)->dest;
	quad->src_a = stack_belowtop(Stack)->dest;
	quad->dest = REG_NEW;
	stack_pop(Stack);
	//stack_pop(Stack);
	//stack_push(Stack, quad);
	stack_set_top(Stack, quad);
	*adv = 1;
	break;

    case INST_START_CMD: /* 105 */
	DEBUG(", startcmd (cmd count = %d), ", TclGetUInt4AtPtr(pc + 5));
	/* 4 bytes are used to indicate where the next command starts,
	 * other 4 are used to indicate the length of this command, and
	 * we also "advance" over the current bytecode. */
	*adv = 9;
	/* XXX Understand when we can't just skip to the start of
	 * the command.*/
	free(quad);
	quad = NULL;
	break;

    case INST_BITXOR: /* 43 */
    case INST_RSHIFT: /* 52 */
    case INST_ADD:    /* 53 */
    case INST_MULT:   /* 55 */
    case INST_EXPON:  /* 99 */
	DEBUG(", arith [%d], ", quad->instruction);
	quad->src_b = stack_pop(Stack)->dest;
	quad->src_a = stack_pop(Stack)->dest;
	quad->dest = REG_NEW;
	stack_push(Stack, quad);
	*adv = 1;
	break;

    default: /* XXX */
	*adv = 1;
	break;
    }

    return quad;
}


/* Release all the memory allocated for the basic blocks. */
void freeblocks(struct BasicBlock *blocks, int count)
{
    int i;
    struct Quadruple *temp;

    for (i = 0; i < count; i++) {
        if (blocks[i].exitcount) {
            free(blocks[i].exitblocks);
        }
        //printf("Block %d\n", i);
        while (blocks[i].quads) {
            temp = blocks[i].quads;
            blocks[i].quads = blocks[i].quads->next;
            //printf("  %p %p %p\n", temp->dest, temp->src_a, temp->src_b);
	    /* XXX Problem: dest, src_a or src_b might be used in more than
	       one place. Reference counting should solve this. */
            /*if (temp->dest) free(temp->dest);
	    if (temp->src_a) free(temp->src_a);
	    if (temp->src_b) free(temp->src_b);
	    temp->dest = temp->src_a = temp->src_b = NULL;*/
            free(temp);
        }
    }
    free(blocks);
}

/* Emacs configuration.
 *
 * Local Variables:
 *   mode: c
 *   c-basic-offset: 4
 *   fill-column: 78
 * End:
 */
