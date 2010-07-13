#include <stdio.h>
#include "tclJitCompile.h"

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
}

void
JIT_bb_output(char *procname, struct BasicBlock *blocks, int numblocks)
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
}
