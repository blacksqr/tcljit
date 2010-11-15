#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tclJitCompile.h"
#include "tclJitOutput.h"
#include "tclJitInsts.h"
#include "tclJitTypeCollect.h"
#include "tclJitCodeGen.h"

#define IS_JUMP_INST1(op) (op == INST_JUMP1 || op == INST_JUMP_TRUE1 || \
			   op == INST_JUMP_FALSE1)

#define IS_JUMP_INST4(op) (op == INST_JUMP4 || op == INST_JUMP_TRUE4 || \
                           op == INST_JUMP_FALSE4)

#define IS_JUMP_INST(op) (IS_JUMP_INST1(op) || IS_JUMP_INST4(op))

#define IS_JIT_CJUMP_INST(op) (op == JIT_INST_JTRUE || op == JIT_INST_JFALSE)

/* Using a temporary stack to convert the stack machine nature of the 
 * TVM to a form of register machine. */
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
new_tclvalue(Tcl_Obj *obj, int flags, int offset)
{
    Value val = malloc(sizeof(*val));

    val->type = jitvalue_tcl;
    val->flags = flags;
    val->offset = offset;
    val->content.obj = obj;
    return val;
}

Value
new_intvalue(int integer)
{
    Value val = malloc(sizeof(*val));

    val->type = jitvalue_int;
    val->content.integer = integer;
    val->flags = 0;
    return val;
}


static int stack_index = 0;
Value
new_register(int reset, int flags, int offset)
{
    static int regnum = 0;
    if (reset) {
        regnum = 0;
        stack_index = 0;
        return NULL;
    }

    Value reg = malloc(sizeof(*reg));

    regnum++;
    reg->type = jitreg;
    reg->flags = flags;
    reg->offset = offset;
    reg->content.vreg.allocated = 0;
    reg->content.vreg.offset = stack_index++;
    reg->content.vreg.regnum = regnum;
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
#define REG_NEW new_register(0, 0, -1)
#define REG_NEW_EX(flags, offset) new_register(0, flags, offset)

struct ObjReg {
    Tcl_Obj *obj;
    Value reg;
};

struct Quadruple *build_quad(ByteCode *, unsigned char *, int *, int, int[],
			     struct ObjReg *);
struct BasicBlock *rearrange_blocks(struct BasicBlock *, int);
void freeblocks(struct BasicBlock *, int);


/* JIT_Compile returns 1 on success, 0 otherwise. */
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
    memset(leaders, 0, code->numCodeBytes * sizeof(int));

    REG_RESETCOUNT;
    compiledLocals = ((Interp *)interp)->varFramePtr->compiledLocals;
    for (i = 0; i < ((Interp *)interp)->varFramePtr->numCompiledLocals; i++) {
        if (compiledLocals[i].value.objPtr != NULL) {
            /* This corresponds to a formal parameter. */
            locals[i].obj = Tcl_DuplicateObj(compiledLocals[i].value.objPtr);
            locals[i].reg = REG_NEW_EX(JIT_VALUE_LOCALVAR | JIT_VALUE_PARAM, i);
            DEBUG("@@%s\n", Tcl_GetString(locals[i].obj));
        } else {
            locals[i].obj = NULL;
            locals[i].reg = REG_NEW_EX(JIT_VALUE_LOCALVAR, i);
        }
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

    pc = code->codeStart;
    DEBUG("%d", *pc);
    pc++;
    for (i = 1; i < code->numCodeBytes; i++) {
        DEBUG(", %d", *pc);
        pc++;
    }
    DEBUG("\n");
#endif

    pc = code->codeStart;
    numblocks = 1;
    leaders[code->numCodeBytes] = code->numCodeBytes;
    pc++;
    /* XXX This might be bugged, what if some kind of index appears
     * somewhere in the code and is the same as a JUMP_INST1 ? */
    for (i = 1; i < code->numCodeBytes; i++) {
        op = *pc;
        if (op == INST_START_CMD) {
            pc += 8;
            i += 8;
        }
        if (IS_JUMP_INST1(op)) {
            leaders[i + *(pc + 1)] = 1;
            leaders[i + 2] = 1;
            pc++; i++;
        } else if (op == INST_JUMP_TRUE4) { /* XXX Added 05/11/10 for testing. */
            int result;
            result = *(pc + 1) << 24;
            result+= *(pc + 2) << 16;
            result+= *(pc + 3) << 8;
            result+= *(pc + 4);
            leaders[i + result] = 1;
            leaders[i + 5] = 1;
            pc += 3; i += 3;
        }
        pc++;
    }
    /* XXX Not working correctly for a proc like:
     * proc x {n} {
     *   while {$n > 0} {
     *       set n 0
     *   }
     * }*/
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

    /* There is a special block, so we allocate memory for this
     * other block too. */
    blocks = malloc((numblocks + 1) * sizeof(struct BasicBlock));

    /* Build basic blocks. */
    int got_done = 0;
    pc = code->codeStart;
    runningcount = 1;
    for (i = 0; i < numblocks; j++, i++) {
        blocks[i].id = i;
        blocks[i].quads = calloc(1, sizeof(struct Quadruple));
        blocks[i].quads->next = NULL;
        blocks[i].exitcount = 0;
        ptr = blocks[i].quads;
        quadcount = leaders[j + 1] - leaders[j];
        //printf("QUADCOUNT = %d\n", quadcount);
        for (k = 0; k < quadcount; k++) {
            advance = 0;
            quad = build_quad(code, pc, &advance, runningcount + k,
			      bc_to_bb, locals);
            if (advance < 0) {
                /* Instruction not supported. */
                DEBUG("Instruction '%d' not supported.\n", *pc);
                goto err;
            }
            k += (advance - 1);
            pc += advance;
            //printf("KKKK = %d\n", k);

            if (quad != NULL) {
                /* XXX Testing: Tcl generates two consecutives INST_DONE
                 * instructions sometimes, I don't want to consider the
                 * second one. */
                if (quad->instruction == INST_DONE) {
                    if (got_done) {
                        free(quad);
                        continue;
                    }
                    got_done = 1;
                } else {
                    got_done = 0;
                }
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
            /* XXX Something very weird here, if I remove the DEBUG calls
             * everything tears down. For now there are these two fprintf
             * calls in place of DEBUG. */
            if (i + 1 == numblocks) {
                /* This is the "exit" block. */
                DEBUG("exit block: %d %d\n", i,
		      GET_INT(blocks[i].lastquad->dest));
                //fprintf(stderr, "%d\n", i);

                blocks[i].exitcount = 1;
                blocks[i].exitblocks = malloc(1 * sizeof(int));
                blocks[i].exitblocks[0] = GET_INT(blocks[i].lastquad->dest);
            } else {
                DEBUG("exits %d: %d %d\n", i, i + 1,
		      GET_INT(blocks[i].lastquad->dest));
                //fprintf(stderr, "%d", -1);

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

#if DEBUGGING
    JIT_bb_output(TclGetString(procName), blocks, numblocks);
    printf("\nbefore rearrange -------------\n\n");
#endif

    /* Rearrange blocks so move instructions issuing Tcl objects
     * live in a special block. The instructions that happen to be
     * reallocated, are replaced by a NOP instruction. */
    struct BasicBlock *special = rearrange_blocks(blocks, numblocks);
    if (special == NULL) {
        perror("Weird!\n");
        abort();
    }
    blocks[numblocks] = *special;

#if DEBUGGING
    JIT_bb_output(TclGetString(procName), blocks, numblocks + 1);
    printf("\n-------------\n\n");
#endif

    ncode = JIT_CodeGen(blocks, numblocks, stack_index);
    if (ncode == NULL) {
    //    DEBUG("Error on JIT_CodeGen, NULL native code.\n");
err:
        return 0;
    }
    code->procPtr->jitproc.ncode = ncode;

    /* XXX Missing free for "locals". */
    freeblocks(blocks, numblocks);

    return 1;
}


void
dfs(struct BasicBlock *CFG, int visiting, int visited[],
        struct Quadruple *quadsptr)
{
    int i, to_visit;
    struct BasicBlock *block = &(CFG[visiting]);
    struct Quadruple *ptr, *quad;
    visited[visiting] = 1;

    for (ptr = block->quads->next; ptr; ptr = ptr->next) {
        if (ptr->instruction != JIT_INST_MOVE)
            continue;

        /* XXX Checar porque a INCR nao cai no segundo caso aqui. */

        if (ptr->src_a->type == jitvalue_tcl) {
            quad = calloc(1, sizeof(struct Quadruple));
            quad->instruction = ptr->instruction;
            quad->src_a = ptr->src_a;
            quad->dest = ptr->dest;
            quadsptr->next = quad;
            quadsptr = quadsptr->next;
            ptr->instruction = JIT_INST_NOP;
        } else if (ptr->src_a->flags & JIT_VALUE_PARAM) {
            quad = calloc(1, sizeof(struct Quadruple));
            quad->instruction = INST_LOAD_SCALAR1;
            quad->src_a = ptr->src_a;
            quad->dest = ptr->src_a;
            quadsptr->next = quad;
            quadsptr = quadsptr->next;
            ptr->src_a->flags &= ~JIT_VALUE_PARAM;
        }
    }

    for (i = 0; i < block->exitcount; i++) {
        to_visit = block->exitblocks[i];
        if (!visited[to_visit]) {
            dfs(CFG, to_visit, visited, quadsptr);
        }
    }
}

struct BasicBlock *
rearrange_blocks(struct BasicBlock *CFG, int bbcount)
{
    int visited[bbcount];
    struct BasicBlock *special;

    if (!bbcount)
        return NULL;

    memset(visited, 0, sizeof(visited));

    special = calloc(1, sizeof(struct BasicBlock));
    special->id = bbcount;
    special->exitcount = 1;
    special->exitblocks = malloc(1 * sizeof(int));
    special->exitblocks[0] = 0;
    special->quads = calloc(1, sizeof(struct Quadruple));
    special->quads->next = NULL;
    dfs(CFG, 0, visited, special->quads);
    return special;
}


struct Quadruple *
build_quad(ByteCode *code, unsigned char *pc, int *adv, int pos, int bc_to_bb[],
	   struct ObjReg *locals)
{
    struct Quadruple *quad = calloc(1, sizeof(struct Quadruple));

    quad->instruction = *pc;
    DEBUG("%d ", quad->instruction);

    switch (quad->instruction) {

    case INST_DONE: /* 0 */
    /* XXX INST_DONE precisa estar presente em algum lugar do
     * codigo gerado para sinalizar que algum Tcl_Obj* precisa ser posto
     * como resultado (Tcl_SetObjResult). */
        DEBUG("(done), ");
        quad->instruction = JIT_INST_SAVE;
        quad->src_a = stack_top(Stack)->dest;
        //quad->src_a = stack_pop(Stack)->dest; /* XXX 1/11/10 */
        *adv = 1;
        break;

    case INST_PUSH1: /* 1 */
	DEBUG(", pushing %d, ", *(pc + 1));
	quad->instruction = JIT_INST_MOVE;
	quad->dest = REG_NEW;
	quad->src_a = new_tclvalue(code->objArrayPtr[*(pc + 1)],
                JIT_VALUE_OBJARRAY, *(pc + 1));
        /* XXX Test for dealing with empty ("") literals. */
        long int result;
        if (Tcl_GetLongFromObj(NULL, code->objArrayPtr[*(pc + 1)],
                    &result) != TCL_OK) {
            quad->src_a = new_intvalue(0x14151415);
        }

	stack_push(Stack, quad);
	*adv = 2; /* 1 for the push instruction plus 1 for the index */
	break;

    case INST_INVOKE_STK1: /* 6 */
	DEBUG(", invoking (argc = %d), ", *(pc + 1));
	quad->instruction = JIT_INST_CALL;
	/* src_a: register "holding" the objv address.
	 * src_b: parameters. */
	quad->src_a = stack_get_at(Stack, *(pc + 1) - 1)->dest;
	//quad->src_b = new_listvalue(Stack, *(pc + 1) - 1);
	*adv = -1; /* Should be "2", but the call instruction is
                      not supported yet. */
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

    case INST_JUMP_TRUE4: /* 37 */ {
        int offset;
        offset = *(pc + 1) << 24;
        offset+= *(pc + 2) << 16;
        offset+= *(pc + 3) << 8;
        offset+= *(pc + 4);
	DEBUG(", offset (ljt %d %d), ", offset, bc_to_bb[pos + offset]);
	quad->instruction = JIT_INST_JTRUE;
	quad->src_a = stack_pop(Stack)->dest;
	quad->dest = new_intvalue(bc_to_bb[pos + offset]);
        *adv = 5;
        break;
    }

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
        /* XXX Simplified. */
	DEBUG(", loadscalar (%d) << %p >>, ", *(pc + 1), locals[*(pc + 1)].reg);

//	Tcl_Obj *local_obj = locals[*(pc + 1)].obj;

	quad->instruction = JIT_INST_MOVE;
	quad->dest = REG_NEW;
//	quad->src_a = new_tclvalue(local_obj, JIT_VALUE_LOCALVAR, *(pc + 1));
        quad->src_a = locals[*(pc + 1)].reg;

	stack_push(Stack, quad); /* XXX 1/11/10 */
	*adv = 2;
	break;
    }

    case INST_STORE_SCALAR1: { /* 17 */
	/* XXX Simplified this opcode for now. */
	DEBUG(", storescalar (%d @ %p), ",
	      *(pc + 1), locals[*(pc + 1)].reg);

	struct Quadruple *top = stack_top(Stack);

	quad->instruction = JIT_INST_MOVE;
	quad->dest = locals[*(pc + 1)].reg;
	//locals[*(pc + 1)].obj = top->src_a->content.obj; /* 04/11/10 */
	quad->src_a = top->dest;

	stack_push(Stack, quad);
	*adv = 2;
	break;
    }

    case INST_INCR_SCALAR1_IMM: /* 29 */
	DEBUG(", incr_imm (%d %d), ", *(pc + 1), *(pc + 2));
        /* XXX quad->src_a needs to be able to be converted to an integer,
         * could signal this using JIT_ResolveType here. */
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

    case INST_EQ: /* 45 */
    case INST_NEQ: /* 46 */
    case INST_GT: /* 48 */
    case INST_LE: /* 49 */
    case INST_GE: /* 50 */
    case INST_LT: /* 47 */
        if (quad->instruction == INST_GT) {
            DEBUG(", top > belowTop, ");
        } else if (quad->instruction == INST_GE) {
            DEBUG(", top >= belowTop, ");
        } else if (quad->instruction == INST_LT) {
            DEBUG(", top < belowTop, ");
        } else if (quad->instruction == INST_EQ) {
            DEBUG(", top == belowTop, ");
        } else if (quad->instruction == INST_NEQ) {
            DEBUG(", top != belowTop, ");
        } else
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

#if 0
    case INST_BREAK: /* 65 XXX */
        *adv = 1;
        break;
#endif

    case INST_LNOT: /* 61 */
        DEBUG(", !a, ");
        quad->src_a = stack_pop(Stack)->dest;
        quad->dest = REG_NEW;
        stack_push(Stack, quad);
        *adv = 1;
        break;

    case INST_MOD: /* 57 */
        if (!JIT_TYPE_RESOLVED(code->procPtr, pos)) {
            DEBUG("Won't be able to genereate code for modulus.\n");
            /* XXX Assuming integer. */
            JIT_ResolveType(code->procPtr, pos, TCL_NUMBER_LONG);
        }
        DEBUG(", a %% b, ");
        quad->src_b = stack_pop(Stack)->dest;
        quad->src_a = stack_pop(Stack)->dest;
        quad->dest = REG_NEW;
        quad->dest->content.vreg.type = JIT_TYPE_GET(code->procPtr, pos);
        stack_push(Stack, quad);
        *adv = 1;
        break;

    case INST_BITXOR: /* 43 */
    case INST_RSHIFT: /* 52 */
    case INST_ADD:    /* 53 */
    case INST_SUB:    /* 54 */
    case INST_MULT:   /* 55 */
    case INST_DIV:    /* 56 */
    case INST_EXPON:  /* 99 */
        if (!JIT_TYPE_RESOLVED(code->procPtr, pos)) {
            /*DEBUG("Won't be able to genereate code.\n");
            *adv = -1;*/
            /* XXX Assuming integer. */
            JIT_ResolveType(code->procPtr, pos, TCL_NUMBER_LONG);
//            break;
        }
	DEBUG(", arith [%d:%d], ", quad->instruction,
                JIT_TYPE_GET(code->procPtr, pos));
	quad->src_b = stack_pop(Stack)->dest;
	quad->src_a = stack_pop(Stack)->dest;
	quad->dest = REG_NEW;
        quad->dest->content.vreg.type = JIT_TYPE_GET(code->procPtr, pos);
	stack_push(Stack, quad);
	*adv = 1;
	break;

    default: /* Not supported */
	*adv = -1; /* Sinalization for lack of support. */
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
