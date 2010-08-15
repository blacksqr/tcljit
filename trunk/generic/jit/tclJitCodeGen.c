/*
 * Created:  20/07/2010 23:29:56
 *
 * Author:  Guilherme Polo, ggpolo@gmail.com
 *
 */
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    /* XXX Sections under _WIN32 weren't tested. */
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/mman.h>
#endif
#include "tclJitCompile.h"
#include "tclJitCodeGen.h"
#include "tclJitInsts.h"
#include "arch/arch.h"

//#if DEBUGGING
//#include "tclJitOutput.h"
static const char *value_type_str[] = {
        /* jitvalue_tcl = "Tcl", jitvalue_int = "int", jitreg = "Reg",
         *     jitvalue_long = "long", jitvalue_double = "double"*/
        "Tcl", "int", "Reg", "long", "double"
};
//#endif

static void dfs(struct BasicBlock *, int, int *, MCode *);
static void codegen(struct Quadruple *, MCode *);

#ifdef _WIN32
DWORD
#else
int
#endif
pagesize(void)
{
#ifdef _WIN32
    DWORD pagesize;
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return si.dwAllocationGranularity;
#else
    return getpagesize();
#endif
}

void *
newpage(void *size)
{
    void *page;

#ifdef _WIN32
    page = VirtualAlloc(NULL, *((DWORD *)size), MEM_COMMIT,
            PAGE_EXECUTE_READWRITE);
    if (!page) {
        perror("VirtualAlloc");
        exit(1);
    }
#else
    page = mmap(NULL, *((size_t *)size), PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_ANON | MAP_PRIVATE, 0, 0);
    if (page == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
#endif

    return page;
}

unsigned char *
JIT_CodeGen(struct BasicBlock *CFG, int bbcount)
{
    MCode code;
    int visited[bbcount];

    if (!bbcount) {
        /* Shouldn't happen. No basic block to generate code for. */
	return NULL;
    }

    code.used = 0;
    code.limit = pagesize();
    code.mcode = newpage(&code.limit);
    code.codeEnd = code.mcode;

    memset(visited, 0, sizeof(visited));

    /* XXX There could be a register allocation phase here. */

    PROLOGUE(code.codeEnd);

    /* Assuming that node 0 in the CFG is the source node. */
    dfs(CFG, 0, visited, &code);

    EPILOGUE(code.codeEnd);

    int i;
    for (i = 0; i < (code.codeEnd - code.mcode); i++) {
        printf("0x%X ", code.mcode[i]);
    }
    printf("\n");
    printf(">>> %d <<<\n", (code.codeEnd - code.mcode));

    return code.mcode;

    /* XXX At some point either VirtualFree(code.mcode, 0, MEM_RELEASE)
     * or munmap(code.mcode, code.limit) needs to be called. */
}

static void
dfs(struct BasicBlock *CFG, int visiting, int visited[], MCode *code)
{
    int i, to_visit;
    struct BasicBlock *block = &(CFG[visiting]);

    visited[visiting] = 1;

    DEBUG("##CG Visiting %d\n", block->id);
    /* Generate code for this block. */
    codegen(block->quads, code);

    //for (i = block->exitcount - 1; i >= 0; i--) {
    for (i = 0; i < block->exitcount; i++) {
        /* Go to the next basic block. */
        to_visit = block->exitblocks[i];
        if (!visited[to_visit]) {
            dfs(CFG, to_visit, visited, code);
        }
    }
}


static void
codegen(struct Quadruple *quads, MCode *code)
{
    struct Quadruple *ptr;

    if (!quads || !(quads->next)) {
        /* XXX Shouldn't be empty. No quadruples in a basic block. */
        abort();
        return;
    }

    for (ptr = quads->next; ptr; ptr = ptr->next) {
        switch (ptr->instruction) {

        case JIT_INST_MOVE:
            DEBUG("INST_MOVE: %s <- %s\n", value_type_str[ptr->dest->type],
                    value_type_str[ptr->src_a->type]);
            /* XXX Possibly expand code.mcode before advancing. */
            if (ptr->dest->type == jitreg) {
                if (ptr->src_a->type == jitreg) {
                    MOV_REG_REG(code->codeEnd,
                            allocReg(ptr->src_a),
                            allocReg(ptr->dest));
                } else if (ptr->src_a->type == jitvalue_tcl) {
                    MOV_MEM_REG(code->codeEnd,
                            (ptrdiff_t)ptr->src_a->content.obj,
                            allocReg(ptr->dest));
                } else {
                    /* XXX Unsupported. */
                }
            } else {
                /* XXX Unsupported. */
            }
            break;
        }
    }
}
