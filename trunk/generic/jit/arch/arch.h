#ifndef TCLJIT_ARCH_H
#define TCLJIT_ARCH_H

#ifdef __i386__
    #include "x86/mcode.h"
#else
    #ifdef TCL_JIT
        /* XXX Instead of stopping the compilation with an error we
         * could simply not use the JIT system. */
        #error "Compiler and/or architecture not supported."
    #endif
#endif

#endif /* TCLJIT_ARCH_H */
