#ifndef TCLJIT_TYPECOLLECT_H
#define TCLJIT_TYPECOLLECT_H

#include "tclInt.h"

#define JIT_TYPE_RESOLVED(procPtr, pos)			\
    procPtr->jitproc.bytecodeTypes[pos].resolved

#define JIT_TYPE_COLLECT(procPtr, pos, bctype)		\
    procPtr->jitproc.bytecodeTypes[pos].type = bctype

#define JIT_TYPE_GET(procPtr, pos)		\
    procPtr->jitproc.bytecodeTypes[pos].type

#define JIT_BCTYPE_TRUE 128
#define JIT_BCTYPE_FALSE 129

void JIT_ResolveType(Proc *, int, int);

#endif /* TCLJIT_TYPECOLLECT_H */

/* Emacs configuration.
 *
 * Local Variables:
 *   mode: c
 *   c-basic-offset: 4
 *   fill-column: 78
 * End:
 */
