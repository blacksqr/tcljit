#ifndef TCLJIT_EXEC_H
#define TCLJIT_EXEC_H

#define JIT_RESULT_INTERPRET 1024

#define JIT_RUN(ncode, interp, code) \
    ((int (*)(void *, void *))ncode)(interp, code)

#endif /* TCLJIT_EXEC_H */
