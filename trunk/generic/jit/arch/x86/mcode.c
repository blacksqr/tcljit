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

struct needPatching {
    unsigned char *addr;
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
    }
}

int
allocReg(void *elem)
{
    /* XXX */
    return EAX;
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
            patch->size = 4;
            patch->addr = malloc(patch->size * sizeof(char));
            patch->used = 0;
            he = Tcl_CreateHashEntry(&toPatch, label, &notused);
            Tcl_SetHashValue(he, patch);
        }
        patch = Tcl_GetHashValue(he);
        ADJUST_SIZE_PATCHLIST(patch);
        patch->addr[patch->used++] = ((code->codeEnd) - (code->mcode));
        result = 0x00;
    } else {
        result = ((ptrdiff_t)Tcl_GetHashValue(he));
    }

    return result;
}

void
jumpEqual(struct MCode *code, const char *label)
{
    *(code->codeEnd)++ = 0x74;
    *(code->codeEnd)++ = getRelative(code, label);
}

void
jumpNotEqual(struct MCode *code, const char *label)
{
    *(code->codeEnd)++ = 0x75;
    *(code->codeEnd)++ = getRelative(code, label);
}

void
jumpTo(struct MCode *code, const char *label)
{

    *(code->codeEnd)++ = 0xEB;
    *(code->codeEnd)++ = getRelative(code, label);
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
            *(code->mcode + patch->addr[i]) = (addr - patch->addr[i] - 1);
        }

        free(patch->addr);
        free(patch);
        Tcl_DeleteHashEntry(he);
    }
}
