#include <stdio.h>
#include <stdlib.h> /* XXX malloc vai provavelmente precisar ser substituído */
#include <string.h>
#include "tclJitCompile.h"
#include "tclJitOutput.h"

#define DEBUGGING 1 /* XXX */

#define DEBUG(str, vaargs...) do { \
    if (DEBUGGING) fprintf(stderr, str, ##vaargs); \
} while (0)

#define IS_JUMP_INST(op) (op == INST_JUMP1 || op == INST_JUMP4 || \
        op == INST_JUMP_TRUE1 || op == INST_JUMP_TRUE4 || \
        op == INST_JUMP_FALSE1 || op == INST_JUMP_FALSE4)

/* XXX Acho que na verdade não vai dar certo usar malloc, precisa de outro
 * gerenciador (provavelmente vou usar algo do Tcl do que implementar outro).
 */

/*typedef struct Value *Value;*/
//typedef struct Type *Type;
/*
struct Quadruple {
    void *dest;
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
    enum { jitvalue_tcl, jitvalue_int } type;
    union {
        Tcl_Obj *obj;
        int integer;
    } content;
};*/

struct stack {
    void *v;
    struct stack *next;
}; /* Used to convert stack machine to register machine. */

struct stack *Stack;

struct stack *
stack_new()
{
    struct stack *s = malloc(sizeof(struct stack));
    s->next = NULL;
    return s;
}

void
stack_push(struct stack *s, void *v)
{
    struct stack *n = malloc(sizeof(struct stack));
    n->v = v;
    n->next = s->next;
    s->next = n;
}

void *
stack_pop(struct stack *s)
{
    struct stack *temp = s->next;
    void *v = temp->v;

    s->next = s->next->next;
    free(temp);

    return v;
}

/*
void
print_value(Value v)
{
    switch (v->type) {
        case jitvalue_tcl:
            printf("Tcl_Obj: %s\n", TclGetString(v->content.obj));
            break;
        case jitvalue_int:
            printf("Int: %d\n", v->content.integer);
            break;
        default:
            perror("print_value");
            break;
    }
}*/

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

/* XXX */
#define GET_INT(value) value->content.integer

struct Quadruple *build_quad(ByteCode *, unsigned char *, int *, int, int[],
        Var *);
void freeblocks(struct BasicBlock *, int);

/*
void 
output(char *procname, struct BasicBlock *blocks, int numblocks)
{
    FILE *f;
    struct Quadruple *ptr;
    char name[strlen(procname) + 4 + 1];
    int i, j;

    snprintf(name, sizeof name, "%s.dot", procname);
    if (!(f = fopen(name, "w"))) {
        perror("fopen");
    }

    for (i = 0; i < numblocks; i++) {
        printf("Block %d (%p)\n", i, &blocks[i]);
        ptr = blocks[i].quads->next;
        while (ptr) {
            printf("  %d\n", ptr->instruction);
            ptr = ptr->next;
        }
        printf("  Exit points:");
        for (j = 0; j < blocks[i].exitcount; j++) {
            printf("    %p", blocks[i].exit[j]);
        }
        putchar('\n');
    }

    fclose(f);
}*/


/* XXX Preciso coletar tipos durante execução (TclExecute) também. Nem tanto
 * para montar o CFG e a representação em SSA, mas para gerar código. */

/* Byte code obsoletos (que eu não vou me preocupar): INST_CALL_FUNC1,
 * INST_CALL_BUILTIN_FUNC1 */

int
JIT_Compile(Tcl_Obj *procName, Tcl_Interp *interp, ByteCode *code)
{
    /* code->source contém o código fonte que gerou esses bytecodes. */
    /* code->codeStart é o que parece que mais me interessa no momento. */
    /*printf("oi! %p (%s)\n", code, code->source);*/
    int i, j, k;
    int runningcount, advance, quadcount;
    int leaders[code->numCodeBytes + 1], bc_to_bb[code->numCodeBytes + 1];
    int numblocks;
    unsigned char *pc, op;
    Var *compiledLocals;
    struct Quadruple *ptr;
    struct BasicBlock *blocks;

    Stack = stack_new();
    compiledLocals = ((Interp *)interp)->varFramePtr->compiledLocals;
    pc = code->codeStart;
    memset(leaders, 0, code->numCodeBytes * sizeof(int));

    //printf("numCodeBytes = %d, numSrcBytes = %d, codeStartlen = %d\n",
    //    code->numCodeBytes, code->numSrcBytes, strlen(code->codeStart));
    printf("proc = %s\n", TclGetString(procName));
    DEBUG("numCodeBytes = %d, numLitObjects = %d\n", code->numCodeBytes,
            code->numLitObjects);
    for (i = 0; i < code->numLitObjects; i++) {
        DEBUG(">>%s\n", Tcl_GetString(code->objArrayPtr[i]));
    }

    numblocks = 1;
    leaders[code->numCodeBytes] = code->numCodeBytes;
    //leaders[0] = 1;
    pc++;
    for (i = 1; i < code->numCodeBytes; i++) {
        op = *pc;
        if (IS_JUMP_INST(op)) {
            leaders[i + *(pc + 1)] = 1; /* XXX Pode ocupar 4 bytes também. */
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
    //printf("Lideres (%d): ", numblocks);
    k = j;
    int xx = 0;
    for (i = 0; i < numblocks; i++, j++) {
        //printf("%d (%d), ", leaders[j], leaders[j + 1] - leaders[j]);
        for (xx = leaders[j]; xx < leaders[j] + leaders[j + 1] - 1; xx++) {
            bc_to_bb[xx] = i;
        }
    }
    /*for (i = 0; i < code->numCodeBytes; i++) {
        printf("%d | ", bc_to_bb[i]);
    }*/
    j = k;
    printf("\n");

    blocks = malloc(numblocks * sizeof(struct BasicBlock));
    pc = code->codeStart;
    for (i = 0; i < code->numCodeBytes; i++) {
        DEBUG("%d, ", *pc);
        pc++;
    }
    DEBUG("\n");

    /* Build basic blocks. */
    pc = code->codeStart;
    runningcount = 0;
    //printf("<<<< %d %d >>>>\n", sizeof(struct Quadruple), sizeof(struct Quadruple *));
    for (i = 0; i < numblocks; j++, i++) {
        blocks[i].quads = calloc(1, sizeof(struct Quadruple));
        blocks[i].quads->next = NULL;
        blocks[i].exitcount = 0;
        ptr = blocks[i].quads;
        quadcount = leaders[j + 1] - leaders[j];
        for (k = 0; k < quadcount; k++) {
            advance = 0;
            ptr->next = build_quad(code, pc, &advance,
                    runningcount + k, bc_to_bb, compiledLocals);
            ptr = ptr->next;
            k += (advance - 1);
            pc += advance;
        }
        runningcount += k;
        blocks[i].lastquad = ptr;
        //printf("%d -> %d\n", leaders[j], leaders[j + 1] - 1);
        //printf("%d %p\n", i, ptr);
    }

    /* Add initial flow information to the blocks. */
    for (i = 0; i < numblocks; i++) {
        if (!IS_JUMP_INST(blocks[i].lastquad->instruction)) {
            if (i + 1 == numblocks) {
                /* This is the "exit" block. */
                DEBUG("exit block: %d\n", i);
            } else {
                blocks[i].exitcount = 1;
                blocks[i].exit = malloc(sizeof(struct BasicBlock *));
                blocks[i].exit[0] = &blocks[i + 1];
                DEBUG("exits %d: %d\n", i, i + 1);
            }
        } else {
            if (i + 1 == numblocks) {
                /* This is the "exit" block. */
                blocks[i].exitcount = 1;
                blocks[i].exit = malloc(sizeof(struct BasicBlock *));
                blocks[i].exit[0] = &blocks[GET_INT(blocks[i].lastquad->src_a)];
                DEBUG("exit block: %d %d\n", i,
                        GET_INT(blocks[i].lastquad->src_a));
            } else {
                blocks[i].exitcount = 2;
                blocks[i].exit = malloc(2 * sizeof(struct BasicBlock *));
                blocks[i].exit[0] = &blocks[i + 1];
                blocks[i].exit[1] = &blocks[GET_INT(blocks[i].lastquad->src_a)];
                DEBUG("exits %d: %d %d\n", i, i + 1,
                        GET_INT(blocks[i].lastquad->src_a));
            }
        }
    }

    JIT_bb_output(TclGetString(procName), blocks, numblocks);
    printf("\n-------------\n\n");

    freeblocks(blocks, numblocks);

    return 0;
}

struct Quadruple *
build_quad(ByteCode *code, unsigned char *pc, int *adv, int pos, int bc_to_bb[],
        Var *locals)
{
    struct Quadruple *quad = calloc(1, sizeof(struct Quadruple));

    quad->instruction = *pc;
    DEBUG("%d ", quad->instruction);

    /* XXX O que fazer com INST_DONE ? Pular e retornar próxima instrução ? */

    switch (quad->instruction) {

        case INST_PUSH1: // 1
            DEBUG(", pushing %d, ", *(pc + 1));
            quad->src_a = new_tclvalue(code->objArrayPtr[*(pc + 1)]);
            stack_push(Stack, quad->src_a);
            //printf(", pushing %d (%s), ", *(pc + 1),
            //    Tcl_GetString(code->objArrayPtr[*(pc + 1)]));
            *adv = 2; /* 1 para inst. push e 1 para objeto */
            break;

        case INST_INVOKE_STK1: // 6
            DEBUG(", invocando (argc = %d), ", *(pc + 1));
            quad->src_a = new_intvalue(*(pc + 1));
            *adv = 2;
            break;

        case INST_JUMP_FALSE1: // 38
            DEBUG(", offset (jf %d %d), ", (char)*(pc + 1),
                    bc_to_bb[pos + (char)*(pc + 1)]);
            quad->src_a = new_intvalue(bc_to_bb[pos + (char)*(pc + 1)]);
            *adv = 2;
            break;

        case INST_JUMP_TRUE1: // 36
            DEBUG(", offset (jt %d %d), ", (char)*(pc + 1),
                    bc_to_bb[pos + (char)*(pc + 1)]);
            quad->src_a = new_intvalue(bc_to_bb[pos + (char)*(pc + 1)]);
            *adv = 2;
            break;

        case INST_JUMP1: // 34
            DEBUG(", jump (%d %d), ", (char)*(pc + 1),
                    bc_to_bb[pos + (char)*(pc + 1)]);
            quad->src_a = new_intvalue(bc_to_bb[pos + (char)*(pc + 1)]);
            *adv = 2;
            break;

        case INST_POP: // 3
            DEBUG(", pop, ");
            *adv = 1;
            break;

        case INST_LOAD_SCALAR1: // 10
            DEBUG(", scalar (%d), ", *(pc + 1));
            /* valor escalar é obtido em compiledLocals[*(pc + 1)].
             * compiledLocals vem de
             * 		((* Interp)interp)->varFramePtr->compiledLocals */
            *adv = 2;
            break;

        case INST_STORE_SCALAR1: // 17
            DEBUG(", storescalar (%d flags: %d), ", *(pc + 1),
                    locals[*(pc + 1)].flags);
            if (!(locals[*(pc + 1)].flags)) {
                /* XXX Considerar os demais casos depois. */
                /* Top of stack is used to define the value for this
                 * scalar. */
                /* XXX */
            }
            *adv = 2;
            break;

        case INST_INCR_SCALAR1_IMM: // 29
            DEBUG(", incr_imm (%d %d), ", *(pc + 1), *(pc + 2));
            /* *(pc + 1) em compiledLocals, *(pc + 2) == um inteiro */
            *adv = 3;
            break;

        case INST_LT: // 47
            DEBUG(", topo 1 < topo 2, ");
            *adv = 1;
            break;

        case INST_START_CMD: // 105
            DEBUG(", startcmd (cmd count = %d), ", TclGetUInt4AtPtr(pc + 5));
            /* 4 bytes are used to indicate where the next command starts,
             * other 4 are used to indicate the length of this command, and
             * we also "advance" over the current bytecode. */
            *adv = 9;
            break;

        case INST_ADD: // 53
            DEBUG(", add *os dois no topo*, ");
            *adv = 1;
            break;

        case INST_RSHIFT: // 52
            DEBUG(", rshift, ");
            *adv = 1;
            break;

        case INST_BITXOR: // 43
            DEBUG(", xor, ");
            *adv = 1;
            break;

        case INST_EXPON: // 99
            DEBUG(", exp, ");
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
            free(blocks[i].exit);
        }
        //printf("Bloco %d\n", i);
        while (blocks[i].quads) {
            temp = blocks[i].quads;
            blocks[i].quads = blocks[i].quads->next;
            //printf("  %p %p %p\n", temp->dest, temp->src_a, temp->src_b);
            if (temp->dest) free(temp->dest);
            if (temp->src_a) free(temp->src_a);
            if (temp->src_b) free(temp->src_b);
            free(temp);
        }
    }
    free(blocks);
}
