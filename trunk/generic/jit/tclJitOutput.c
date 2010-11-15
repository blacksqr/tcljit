#include <stdio.h>
#include <stdlib.h>
#include "tclJitCompile.h"
#include "tclJitOutput.h"
#include "tclJitInsts.h"

char *get_value(Value);

char *
get_value_destcheck(Value src, Value reg)
{
    if (reg->type == jitreg && src->type == jitvalue_tcl) {
        char *buffer = malloc(32);

        switch (reg->content.vreg.type) {
	case TCL_NUMBER_LONG: {
	    /* XXX */
	    long result;
	    if (Tcl_GetLongFromObj(NULL, src->content.obj, &result)
		!= TCL_OK) {
		perror("get_value_destcheck");
	    }
	    snprintf(buffer, 32, "%ld", result);
	    break;
	}
	case -1:
	    free(buffer);
	    return get_value(src);
	    break;
        }
        return buffer;

    } else {
        return get_value(src);
    }
}


char *
get_value(Value v)
{
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
        if (!(v->content.vreg.allocated)) {
            snprintf(buffer, 32, "R%d[%d]", v->content.vreg.regnum,
                    v->content.vreg.offset);
        } else {
            snprintf(buffer, 32, "R%d", v->content.vreg.regnum);
        }
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
    case JIT_INST_INCR:
    case JIT_INST_ADD:
	snprintf(buffer, 3, "+");
	break;
    case INST_SUB:
	snprintf(buffer, 3, "-");
	break;
    case INST_MULT:
	snprintf(buffer, 3, "*");
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
    struct Quadruple *ptr;
    int i, j;

    for (i = 0; i < numblocks; i++) {
        printf("Block %d (%p)\n", i, &blocks[i]);
        ptr = blocks[i].quads->next;
        while (ptr) {
            switch (ptr->instruction) {
            /* XXX Missing free. */
            case JIT_INST_SAVE:
                printf("  RETURN %s\n", get_value(ptr->src_a));
                break;

	    case JIT_INST_MOVE:
		printf("  %s := %s\n", get_value(ptr->dest),
		       get_value_destcheck(ptr->src_a, ptr->dest));
		break;

	    case JIT_INST_GOTO:
		printf("  GOTO B%s\n", get_value(ptr->dest));
		break;

            case JIT_INST_NOP:
                printf("  NOP\n");
                break;

            case INST_GE:
		printf("  %s := %s >= %s\n", get_value(ptr->dest),
		       get_value(ptr->src_a), get_value(ptr->src_b));
		break;
            case INST_GT:
		printf("  %s := %s > %s\n", get_value(ptr->dest),
		       get_value(ptr->src_a), get_value(ptr->src_b));
		break;
	    case INST_EQ:
		printf("  %s := %s == %s\n", get_value(ptr->dest),
		       get_value(ptr->src_a), get_value(ptr->src_b));
		break;
	    case INST_LT:
		printf("  %s := %s < %s\n", get_value(ptr->dest),
		       get_value(ptr->src_a), get_value(ptr->src_b));
		break;
            case INST_LE:
		printf("  %s := %s <= %s\n", get_value(ptr->dest),
		       get_value(ptr->src_a), get_value(ptr->src_b));
                break;

            case INST_LNOT:
                printf("  %s := NOT %s\n", get_value(ptr->dest),
                        get_value(ptr->src_a));
                break;

            case INST_DIV:
		printf("  %s := %s / %s\n", get_value(ptr->dest),
		       get_value(ptr->src_a), get_value(ptr->src_b));
                break;
            case INST_MOD:
		printf("  %s := %s %% %s\n", get_value(ptr->dest),
		       get_value(ptr->src_a), get_value(ptr->src_b));
                break;

            case INST_LOAD_SCALAR1:
                printf("  %s := @PARAM %d\n", get_value(ptr->dest),
                        ptr->src_a->offset);
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

            case JIT_INST_INCR:
                printf("  INC %s\n", get_value(ptr->dest));
                break;

	    case JIT_INST_ADD:
            case INST_SUB:
	    case INST_MULT:
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
            printf("    %d", blocks[i].exitblocks[j]);
        }
        putchar('\n');
    }

    /*fclose(f);*/
}

/* Emacs configuration.
 *
 * Local Variables:
 *   mode: c
 *   c-basic-offset: 4
 *   fill-column: 78
 * End:
 */
