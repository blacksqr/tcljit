/*
 * Created:  15/07/2010 10:53:50
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#ifndef TCLJIT_CONF_H
#define TCLJIT_CONF_H

/* Number of required procedure invocations before JIT compilation triggers. */
#define JIT_REQINVOKE 1

#define JIT_PROCSETUP(ptr) do { \
    ptr->jitproc.eligible = 1; \
    ptr->jitproc.callCount = (JIT_REQINVOKE > 0) ? JIT_REQINVOKE : 1; \
    ptr->jitproc.ncode = NULL; \
} while (0)

#define JIT_UPDATEPROCCOUNT(ptr) ptr->jitproc.callCount--
#define JIT_READYTOCOMPILE(ptr) (ptr->jitproc.callCount == 0)

typedef struct JIT_Proc {
    int eligible;
    unsigned int callCount; /* Starts as CALLCOUNT_BEFORE_JIT and moves down. */
    unsigned char *ncode;   /* XXX Native code for the proc, NULL for now. */
} JIT_Proc;


#endif /* TCLJIT_CONF_H */
