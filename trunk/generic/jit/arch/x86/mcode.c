#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "mcode.h"
#include "tcl.h"

/* XXX Not checking realloc. */
#define ADJUST_SIZE_PATCHLIST(p) do { \
    if (p->used == p->size - 1) { p->size *= 2; p = realloc(p, p->size); } \
} while(0)

static Tcl_HashTable labels, toPatch;
static int labelsTableInitialized = 0;
static int generatedLabelCount;

struct needPatching {
    //unsigned char *addr;
    //unsigned char *addr;
    ptrdiff_t *addr;

    int size, used;
};

void
initLabelTable(void)
{
    if (labelsTableInitialized) {
        Tcl_Panic("labels table already initialized.\n");
    }
    Tcl_InitHashTable(&toPatch, TCL_STRING_KEYS);
    Tcl_InitHashTable(&labels, TCL_STRING_KEYS);
    labelsTableInitialized = 1;
}

void
finishLabelTable(void)
{
    if (labelsTableInitialized) {
        Tcl_DeleteHashTable(&toPatch);
        Tcl_DeleteHashTable(&labels);
        labelsTableInitialized = 0;
        generatedLabelCount = 0;
    }
}


static ptrdiff_t
getRelative(struct MCode *code, const char *label)
{
    ptrdiff_t result;
    int notused;
    Tcl_HashEntry *he;

    he = Tcl_FindHashEntry(&labels, label);

    if (he == NULL) {
        /* Will need to patch. */
        struct needPatching *patch;
        he = Tcl_FindHashEntry(&toPatch, label);
        if (he == NULL) {
            patch = malloc(sizeof(struct needPatching));
            /* XXX Using a somewhat large size because the Tcl hash table
             * seems to have problems with realloc. */
            patch->size = 32;
            //patch->addr = malloc(patch->size * sizeof(char));
            patch->addr = malloc(patch->size * sizeof(ptrdiff_t));
            patch->used = 0;
            he = Tcl_CreateHashEntry(&toPatch, label, &notused);
            Tcl_SetHashValue(he, patch);
        }
        patch = Tcl_GetHashValue(he);
        //ADJUST_SIZE_PATCHLIST(patch); /* See XXX comment above. */
        patch->addr[patch->used++] = ((code->codeEnd) - (code->mcode));
        result = 0x00;
    } else {
        result = ((ptrdiff_t)Tcl_GetHashValue(he));
    }

    return result;
}

/* je rel32 */
void
jumpEqual(struct MCode *code, const char *label)
{
#if 0
    *(code->codeEnd)++ = 0x74; /* jz/je rel8 */
    *(code->codeEnd)++ = getRelative(code, label);
#endif
    int target;

    *(code->codeEnd)++ = 0x0F;
    *(code->codeEnd)++ = 0x84;
    target = getRelative(code, label);
    IMM32(code->codeEnd, target);
}

/* jne rel32 */
void
jumpNotEqual(struct MCode *code, const char *label)
{
#if 0
    *(code->codeEnd)++ = 0x75; /* jnz/jne rel8 */
    *(code->codeEnd)++ = getRelative(code, label);
#endif
    int target;

    *(code->codeEnd)++ = 0x0F;
    *(code->codeEnd)++ = 0x85;
    target = getRelative(code, label);
    if (target != 0) {
        //ptrdiff_t addr = code->codeEnd - code->mcode;
        //printf("******* %s %d %d\n", label, target,
        //        -((code->codeEnd - code->mcode - 2) - target));
        /* Jump backwards. */
        DEBUG("*** Backwards jump.\n");
        target = -((code->codeEnd - code->mcode - 2) - target) - 6;
    }
    IMM32(code->codeEnd, target);
}

/* jmp rel32 */
void
jumpTo(struct MCode *code, const char *label)
{
#if 0
    *(code->codeEnd)++ = 0xEB; /* jmp rel8 */
    *(code->codeEnd)++ = getRelative(code, label);
#endif
    int target;

    *(code->codeEnd)++ = 0xE9;
    target = getRelative(code, label);
    IMM32(code->codeEnd, target);
}

char *
uniqueLabel(void)
{
    char *label = malloc(22 * sizeof(char));
    generatedLabelCount++;

    snprintf(label, 22, "L%d", generatedLabelCount);
    return label;
}

void
makeLabel(struct MCode *code, const char *label)
{
    int newEntry;
    ptrdiff_t addr = (code->codeEnd - code->mcode);
    Tcl_HashEntry *he;

    he = Tcl_CreateHashEntry(&labels, label, &newEntry);
    if (!newEntry) {
        Tcl_Panic("Label %s already exists.\n", label);
    }
    Tcl_SetHashValue(he, addr);

    /* Maybe the just inserted label can solve some pending jumps. */
    he = Tcl_FindHashEntry(&toPatch, label);
    if (he != NULL) {
        int i;
        struct needPatching *patch = Tcl_GetHashValue(he);

        for (i = 0; i < patch->used; i++) {
            DEBUG("PATCH AT %d, %d\n",
                    patch->addr[i],
                    addr - patch->addr[i] - 4);
            //*(code->mcode + patch->addr[i]) = (addr - patch->addr[i] - 1);
            *(code->mcode + patch->addr[i]) = (addr - patch->addr[i] - 4);
            *(code->mcode + patch->addr[i] + 1) = (addr - patch->addr[i] - 4) >> 8;
            *(code->mcode + patch->addr[i] + 2) = (addr - patch->addr[i] - 4) >> 16;
            *(code->mcode + patch->addr[i] + 3) = (addr - patch->addr[i] - 4) >> 24;
        }

        free(patch->addr);
        free(patch);
        Tcl_DeleteHashEntry(he);
    }
}


/* Load local variable into EAX. */
inline void
//load_localvar(struct MCode *code, struct Quadruple *ptr)
load_localvar(struct MCode *code, struct Quadruple *ptr, int stindex)
{
    int reg;
    long int offset;

    reg = EAX;
    /* Copy the first param (Interp) passed to the JIT func to reg. */
    COPY_PARAM_REG(code->codeEnd, 0, reg);

    /* reg is now pointing at an Interp struct. */

    /* Point to the CallFrame struct. */
    offset = offsetof(Interp, varFramePtr);
    MOV_DISP8DREG_REG(code->codeEnd, offset, reg, reg);

    /* Point to the Var structs. */
    offset = offsetof(CallFrame, compiledLocals);
    MOV_DISP8DREG_REG(code->codeEnd, offset, reg, reg);

    if (ptr->src_a->offset) {
        /* Point to the correct element in the Var array. */
        ADD_IMM8_REG(code->codeEnd, sizeof(Var) * ptr->src_a->offset, reg);
    }

    /* Point to the expected Tcl_Obj. */
    offset = offsetof(Var, value.objPtr);
    MOV_DISP8DREG_REG(code->codeEnd, offset, reg, reg);

    /* XXX Testing: get the integer value */
    AND_IMM8_REG(code->codeEnd, -16, ESP); // XXX Alignment!
    PUSH_REG(code->codeEnd, EBX);

    /* Get integer from obj. */
#if 0
    COPY_PARAM_REG(code->codeEnd, 0, ECX);
    PUSH_REG(code->codeEnd, ECX); /* (Interp *) */
    PUSH_REG(code->codeEnd, reg); /* (Tcl_Obj *) */
    PUSH_REG(code->codeEnd, EDX); /* Where to store the integer value. */
#endif
    LEA_DISP8DREG_REG(code->codeEnd, stindex, EBP, EDX);
    //LEA_DISP32DREG_REG(code->codeEnd, stindex, EBP, EDX);

    PUSH_REG(code->codeEnd, EDX);          /* Where to store the integer value. */
    PUSH_REG(code->codeEnd, reg);          /* (Tcl_Obj *) */
    PUSH_DISP8DREG(code->codeEnd, 8, EBP); /* (Interp *) */


    MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_GetLongFromObj, EBX);
    CALL_ABSOLUTE_REG(code->codeEnd, EBX);

    ADD_IMM8_REG(code->codeEnd, 12, ESP);

    POP_REG(code->codeEnd, EBX);

    /* Checking return from Tcl_GetIntFromObj. */
    CMP_IMM8_REG(code->codeEnd, 0, EAX);
    JUMP_NOTEQ(code, "LINTERP");

    /* Save the value. */ /* XXX Isn't it saved already ? */
//    MOV_DREG_REG(code->codeEnd, EDX, EDX);
//    MOV_REG_DISP8DREG(code->codeEnd, EDX, stindex, EBP);
    /* XXX End testing. */

    //return reg;
}

/* Load Tcl object at stack pos. */
inline void
load_tclobject(struct MCode *code, int obindex, int stindex)
{
    int reg;
    long int offset;

    reg = EAX;
    /* Copy the second param (ByteCode) passed to the JIT func to reg. */
    COPY_PARAM_REG(code->codeEnd, 1, reg);

    /* reg is now pointing at an ByteCode struct. */

    /* Point to the Tcl_Obj array of literal objects. */
    offset = offsetof(ByteCode, objArrayPtr);
    MOV_DISP8DREG_REG(code->codeEnd, offset, reg, reg);

    if (obindex) {
        /* Point to the correct object in this array. */
        ADD_IMM8_REG(code->codeEnd, sizeof(Tcl_Obj *) * obindex, reg);
    }

    /* Get the Tcl_Obj address. */
    MOV_DREG_REG(code->codeEnd, reg, reg);

    /* Save reg to the expected stack position. */
//    MOV_REG_DISP8DREG(code->codeEnd, reg, stindex, EBP);
//    return;


    /* XXX Testing (Need to check for empty string before continuing!) */
    /* Convert to an integer. */
    AND_IMM8_REG(code->codeEnd, -16, ESP); // XXX Alignment!
    PUSH_REG(code->codeEnd, EBX);

    //PUSH_REG(code->codeEnd, EDX); /* Where to store the integer value. */
    //printf(">>> %d\n", stindex);
    LEA_DISP8DREG_REG(code->codeEnd, stindex, EBP, EDX);
    //LEA_DISP32DREG_REG(code->codeEnd, stindex, EBP, EDX);

    PUSH_REG(code->codeEnd, EDX);
    PUSH_REG(code->codeEnd, reg); /* (Tcl_Obj *) */
    PUSH_DISP8DREG(code->codeEnd, 8, EBP);

    MOV_IMM32_REG(code->codeEnd, (ptrdiff_t)Tcl_GetLongFromObj, EBX);
    CALL_ABSOLUTE_REG(code->codeEnd, EBX);

    ADD_IMM8_REG(code->codeEnd, 12, ESP);

    POP_REG(code->codeEnd, EBX);

    /* Checking return from Tcl_GetLongFromObj. */
    CMP_IMM8_REG(code->codeEnd, 0, EAX);
    JUMP_NOTEQ(code, "LINTERP");

    /* Save the value. */
//    MOV_DREG_REG(code->codeEnd, EDX, EDX);
//    MOV_REG_DISP8DREG(code->codeEnd, EDX, stindex, EBP);
}
