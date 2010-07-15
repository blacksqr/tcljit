#include <stdio.h>
#include <stdlib.h>
#include "tclJitCompile.h"
#include "tclJitOutput.h"
#include "tclJitInsts.h"

char *
get_value(Value v)
{
    /*int size;
    char *str;*/
    char *buffer = malloc(32);

    snprintf(buffer, 32, "NULL");

    if (!v) {
        return buffer;
    }

    switch (v->type) {
        case jitvalue_tcl:
            //printf("(Tcl_Obj: %s)", TclGetString(v->content.obj));
            snprintf(buffer, 32, "(Tcl_Obj: %p)", v->content.obj);
            /*str = Tcl_GetStringFromObj(v->content.obj, &size);
            printf("%% %p %d %%\n", v->content.obj, size);
            if (size > 20) {
                snprintf(buffer, 32, "(Tcl_Obj: %p)", v->content.obj);
            } else { 
                snprintf(buffer, 32, "(Tcl_Obj: %s)", str);
            }*/
            break;
        case jitvalue_int:
            snprintf(buffer, 32, "%d", v->content.integer);
            break;
        case jitreg:
            snprintf(buffer, 32, "R%d", v->content.regnum);
            break;
        default:
            perror("get_value");
            break;
    }

    return buffer;
}

char *
get_oper(unsigned char op)
{
    char *buffer = malloc(3);

    switch (op) {
        case JIT_INST_ADD:
            snprintf(buffer, 3, "+");
            break;
        case INST_EXPON:
            snprintf(buffer, 3, "**");
            break;
        case INST_BITXOR:
            snprintf(buffer, 3, "^");
            break;
        case INST_RSHIFT:
            snprintf(buffer, 3, ">>");
            break;
        default:
            snprintf(buffer, 3, "?");
            break;
    }
    return buffer;
}

void
JIT_bb_output(char *procname, struct BasicBlock *blocks, int numblocks)
{
    /*FILE *f;*/
    struct Quadruple *ptr;
    /*char name[strlen(procname) + 4 + 1];*/
    int i, j;

    /*snprintf(name, sizeof name, "%s.dot", procname);
    if (!(f = fopen(name, "w"))) {
        perror("fopen");
    }*/

    for (i = 0; i < numblocks; i++) {
        printf("Block %d (%p)\n", i, &blocks[i]);
        ptr = blocks[i].quads->next;
        while (ptr) {
            switch (ptr->instruction) {
                case JIT_INST_MOVE:
                    /* XXX Missing free. */
                    printf("  %s := %s\n", get_value(ptr->dest),
                            get_value(ptr->src_a));
                    break;

                case JIT_INST_GOTO:
                    printf("  GOTO B%s\n", get_value(ptr->dest));
                    break;

                case INST_LT:
                    printf("  %s := %s < %s\n", get_value(ptr->dest),
                            get_value(ptr->src_a), get_value(ptr->src_b));
                    break;

                case JIT_INST_JTRUE:
                    printf("  IF %s GOTO B%s\n", get_value(ptr->src_a),
                            get_value(ptr->dest));
                    break;

                case JIT_INST_JFALSE:
                    printf("  IF NOT %s GOTO B%s\n", get_value(ptr->src_a),
                            get_value(ptr->dest));
                    break;

                case JIT_INST_CALL:
                    printf("  CALL %s (%s) ...\n", get_value(ptr->src_a),
                            "vazio");
                    break;

                case JIT_INST_ADD:
                case INST_EXPON:
                case INST_BITXOR:
                case INST_RSHIFT:
                    printf("  %s := %s %s %s\n", get_value(ptr->dest),
                            get_value(ptr->src_a), get_oper(ptr->instruction),
                            get_value(ptr->src_b));
                    break;

                default:    
                    printf("  %d\n", ptr->instruction);
                    break;
            }
            ptr = ptr->next;
        }
        printf("  Exit points:");
        for (j = 0; j < blocks[i].exitcount; j++) {
            printf("    %p", blocks[i].exit[j]);
        }
        putchar('\n');
    }

    /*fclose(f);*/
}
