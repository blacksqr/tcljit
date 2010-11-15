#ifndef TCLJIT_INSTS_H
#define TCLJIT_INSTS_H

#define JIT_INST_SAVE 0
#define JIT_INST_MOVE 1
#define JIT_INST_CALL 6
#define JIT_INST_GOTO 34
#define JIT_INST_JTRUE 36
#define JIT_INST_JFALSE 38
#define JIT_INST_ADD 53
#define JIT_INST_INCR 29
#define JIT_INST_NOP (LAST_INST_OPCODE + 1)

#endif /* TCLJIT_INSTS_H */

/* Emacs configuration.
 *
 * Local Variables:
 *   mode: c
 *   c-basic-offset: 4
 *   fill-column: 78
 * End:
 */
