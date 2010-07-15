/*
 * Created:  15/07/2010 15:10:07
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#include "tclJitTypeCollect.h"

void
JIT_ResolveType(Proc *procPtr, int pos, int bctype)
{
    int present = procPtr->jitproc.bytecodeTypes[pos].type;

    if (!present) {
        procPtr->jitproc.bytecodeTypes[pos].type = bctype;
        goto resolve;
    }

    switch (bctype) { 
        /* XXX Not considering TCL_NUMBER_WIDE and TCL_NUMBER_NAN yet. */
        case TCL_NUMBER_DOUBLE:
        case TCL_NUMBER_BIG:
            procPtr->jitproc.bytecodeTypes[pos].type = bctype;
            break;
        case TCL_NUMBER_LONG:
            if (present != TCL_NUMBER_DOUBLE && present != TCL_NUMBER_BIG)
                procPtr->jitproc.bytecodeTypes[pos].type = bctype;
            break;
        default:
            Tcl_Panic("retry in a later version");
            break;
    }

resolve:
    procPtr->jitproc.bytecodeTypes[pos].resolved = 1;
}
