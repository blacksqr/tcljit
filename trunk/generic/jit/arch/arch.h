#ifndef TCLJIT_ARCH_H
#define TCLJIT_ARCH_H

#ifdef TCL_JIT
    #ifdef __i386__
        #include "x86/mcode.h"
    #else
        /* XXX Instead of stopping the compilation with an error we
         * could simply not use the JIT system. */
        #error "Compiler and/or architecture not supported."
    #endif
#endif

#endif /* TCLJIT_ARCH_H */
