/*
Copyright 2018 Alex Ovchinikov
Copyright 2011 Jerry Williams Jr

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and 
associated documentation files (the "Software"), to deal in the Software without restriction, 
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
subject to the following conditions:

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS 
OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
Contributors:

Alex Ovchinikov (AlexO) changes list:
- Interactive mode removed (commands BREAK, RESUME)
- Added: support for floating point (see DATATYPE definition)
- Added: fixed-point Q16.16 support (see DATATYPE definition)
- Added: math support for float/fixed point (see ENABLE_MATH)
- Added: run-time stack checking (see ENABLE_CHECK_STACK)
- Tagret can be compiled as run-time only (ENABLE_COMPILER option)
- The bytecode is platform-indpendent now
- The instruction format is: 
	bits 0..7 - instruction index (in optable[]), 
	bits 8..15 - instruction length, 
	bits 16..31 - source code line number
- All global variables moved to TBASIC_CTX struct; code is fully reenterant now.
- Added: p-code load / save functions
- the modulo operator '\' changed to '%'
- Fixed bug "no underscore symbol in identificators'
- FORMAT specifier string can include '%' char. 
    Example: FORMAT "%%1=%, %%2=%", 10, 20 
    will be print: %1=10, %2=20
- DIM arrays allocated from 'dmem' pool (see TBASIC_CTX struct)
  - heap is not used now
- Added: run-time DIM() allocation checking
- Added: compile listing with 'disasembler'
- Added: function for retrieve variable by hash of name
- Some code refactoring
- Added: some tests
- Added: dynamic function support (dynamic function is function (re)defined during run-time)
- Identifiers is not capitalized now
- Added: semicolon character (;) treated as comment begin (same as #)

*/

#ifndef _BASIC_H_
#define _BASIC_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define TYPE_INT32				1
#define TYPE_FX16Q16				2				
#define TYPE_FLOAT32				3

#define ON					1
#define OFF					0

/* MAIN CONFIGURATION OPTIONS */
#define ENABLE_COMPILER				ON
#define ENABLE_CHECK_STACK			ON
#define ENABLE_DASM				ON
#define ENABLE_MATH				OFF

//#define DATATYPE				TYPE_INT32
//#define DATATYPE				TYPE_FX16Q16
#define DATATYPE				TYPE_FLOAT32

#define MAX_STR_LEN				1000

/* CONFIGURATION DEFINES FOR RUN-TIME */
#define FMTSZ					128			/* FORMAT STRING MAX LEN */
#define OPTSZ					64			/* OPCODE INDEX TABLE SIZE */
#define OPNSZ					16			/* OPCODE NAME LEN */
#define SYMSZ					32			/* SYMBOL SIZE */

#if (ENABLE_COMPILER == ON)
/* CONFIGURATION DEFINES FOR COMPILER */
#define PRGSZ					65535		/* PROGRAM SIZE */
#define STRSZ					65535		/* STRING TABLE SIZE */
#define DIMSZ					1000 		/* DIM ARRAY MEMORY POOL*/
#define LOCS					100			/* LOCAL COUNT */
#define VARS					100			/* MAX VARIABLES */
#define STKSZ					1024		/* STACK SIZE */
#endif

#if (ENABLE_MATH == ON) && (DATATYPE == TYPE_INT32)
#error NO MATH SUPPORT FOR INT32 AVAILABLE!
#endif

/* INCLUDES */
#include <setjmp.h>
#include <stdint.h>
#include <stdarg.h>
#if (DATATYPE == TYPE_FLOAT32)
#include <math.h>
typedef float TData;
#elif (DATATYPE == TYPE_INT32)
typedef int32_t TData;
#elif (DATATYPE == TYPE_FX16Q16)
#include <fixmath.h>
typedef fix16_t TData;
#else
#error NO VALID DATATYPE DEFINED!
#endif

/* INTERNAL BOOL REPRESENTATION */
#define BS_FALSE				0
#define BS_TRUE					1

/* EXIT CODES */
#define EXIT_SUCCESS				0
#define EXIT_ERROR				1
#define EXIT_ERROR_COMPILE			2
#define EXIT_ERROR_RUNTIME			3
#define EXIT_ERROR_ALLOC			4
#define EXIT_ERROR_VERSION			5
#define EXIT_BYE				6

/* COMPILER ERROR MESSAGE */
#define ERR_BAD_TOKEN				1
#define ERR_SYNTAX				2
#define ERR_COUNT				3
#define ERR_EXPRESSION				4
#define ERR_COMP_MODE				5
#define ERR_STA					6
#define ERR_TOK_STA				7
#define ERR_OUT_OF_RANGE			8

/* RUN-TIME ERROR MESSAGE */
#define ERR_BOUNDS				1
#define ERR_DIM					2
#define ERR_STACK_OVERFLOW			3
#define ERR_MATH_OVERFLOW			4
#define ERR_DIV_ZERO				5
#define ERR_MOD_ZERO				6
#define ERR_CODE_OVERFLOW			7
#define ERR_INVALID_OPCODE			8
#define ERR_UNDEFINED_FN			9
#define ERR_DYN_FN				10

/* LISTING VERBOSITY */
#define LST_PRINT_REPORT			0x01
#define LST_PRINT_LISTING			0x02
#define LST_PRINT_ALL				0xff

#define GET_OPCODE(V)				(V & 0xff)
#define GET_OPLEN(V)				((V >> 8 ) & 0x00ff)
#define GET_LINEn(V)				((V >> 16) & 0xffff)

/* Dynamic function argument access */
#define DYF_NARG()				((ctx->sp[0]).i)
#define DYF_ARG(N)				(ctx->sp[DYF_NARG()-N+1])
#define DYF_RETVAL(VAL)				(ctx->sp+=(ctx->sp[0]).i, (ctx->sp)->v = VAL )  /*(ctx->sp+=DYF_NARG()+1, (--ctx->sp)->v = VAL )*/

typedef union TValue TValue;
typedef struct TBASIC_CTX TBASIC_CTX;
typedef int32_t(*TCode)(TBASIC_CTX *ctx); /* BYTE-CODE */
typedef void (*TErrorHook)(TBASIC_CTX *ctx, int32_t nerr, int32_t nline, int32_t addr, char *msg); /* ERROR HOOK */
typedef int32_t (*TTraceHook)(TBASIC_CTX *ctx, int32_t addr, int32_t opcode, int32_t line); /* KEYWORD HOOK */
typedef int32_t (*TFormatHook)(char *str, const char *fmt, va_list ap);
typedef int32_t (*TPutchHook)(int32_t c);
#if (ENABLE_COMPILER == ON)
typedef int32_t (*TKwdHook)(TBASIC_CTX *ctx, char *kwd); /* KEYWORD HOOK */
typedef int32_t (*TFunHook)(TBASIC_CTX *ctx, char *kwd, int32_t n); /* FUNCTION CALL HOOK */
#endif

struct TDynDesc
{
	uint32_t name_hash;
	TCode fn;
};

union TValue
{
	TData v;
	int32_t   i;
	void* p;
};

struct TBASIC_CTX
{
	/* COMPILED PROGRAM */
	int32_t* prg;
	int32_t  prg_sz;
	int32_t pc;
	 
	/* RUN-TIME STACK */
	TValue* stk;
	TValue* sp; 
	int32_t stk_sz;

	/* VARIABLE VALUES */
	TValue* var;
	int32_t var_sz;

	/* DIM() memoty pool */
	TValue* dmem;
	int32_t	dmem_sz;

	/* SUBROUTINE LOCAL VAR INDEXES */
	int32_t* sub;
	int32_t  sloc_sz;

	/* STRING TABLE */
	char* stab;
	int32_t stab_sz;

	/* HASH OF VAR/SUB NAME */
	uint32_t* vhash;
	int32_t vhash_sz;

	/* VAR EXPORT TABLE */
	char* vname;
	int32_t vname_sz;

	/* DYNAMIC FUNCTION SUPPORT */
	struct TDynDesc* dynf_list;
	int32_t dynf_sz;

	int32_t dtop; /* TOP INDEX IN dmem */
	TValue ret; /* FUNCTION RETURN VALUE */
	jmp_buf trap; /* TRAP ERRORS */
	int32_t nerr;		/* LAST ERROR CODE */

	TErrorHook errhook; /* Called before trap when error detected */
	TFormatHook fmthook; /* Called from FORMAT */
	TPutchHook putchook; /* Called for output char to console */
	void* user_ptr;

#if (ENABLE_COMPILER == ON)
	int32_t cpc; /*COMPILER PC*/
	char tokn[SYMSZ], *lp; /* LEXER STATE */
	int32_t lnum, tok, ungot; /* LEXER STATE */
	TValue tokv;
	char name[VARS][SYMSZ]; /* VARIABLE NAMES */
	int32_t mode[VARS]; /* 0=NONE, 1=DIM, 2=SUB, 3=FUN */
	int32_t cstk[STKSZ], *csp; /* COMPILER STACK */
	int32_t nvar, cursub, temp, fcompile, ipc, (**opc)(); /* COMPILER STATE */
	char *stabp; /* STRING TABLE PTR*/
	TKwdHook kwdhook; /* KEYWORD HOOK */
	TFunHook funhook; /* FUNCTION HOOK */
#endif
};

/* FUNCTION PROTOTYPES */

/* Low-level init. Assume manual memory allocation */
int32_t bs_init(
	TBASIC_CTX *ctx,
	int32_t* prg, int32_t prg_sz,
	TValue* stk, int32_t stk_sz,
	TValue* var, int32_t var_sz,
	TValue* dmem, int32_t dmem_sz,
	int32_t* sub, int32_t sloc_sz,
	char* stab, int32_t stab_sz,
	TFormatHook fmthook,	// callback for format string function (like vsprintf()); must be defined.
	TPutchHook putchook,	// callback for output char to console (like putchar()); must be defined.
	TErrorHook errhook,		// callback for error processing; can be NULL
	void* user_ptr);

/* Load p-code image from buffer and allocate memory from heap */
int32_t bs_init_load(
	TBASIC_CTX *ctx,
	void* pcode,
	TFormatHook fmthook,	// callback for format string function (like vsprintf()); must be defined.
	TPutchHook putchook,	// callback for output char to console (like putchar()); must be defined.
	TErrorHook errhook,		// callback for error processing; can be NULL
	void* user_ptr);

/* Freeing memory allocated for TBASIC_CTX*/
int32_t  bs_free(TBASIC_CTX *ctx);

/* Get last error desctiption (msg), address (addr) and line number (lnum)*/
int32_t  bs_last_error(TBASIC_CTX *ctx, int32_t* lnum, int32_t* addr, char** msg);

/* Reset program counter and initialize stack */
int32_t	 bs_reset(TBASIC_CTX *ctx);

/* Execute p-code; Call thook() for every command */
int32_t  bs_run(TBASIC_CTX *ctx, TTraceHook thook);

/* Register new user opcode */
int32_t  bs_reg_opcode(TBASIC_CTX *ctx, TCode code, char* name);

/* Register new user-defined dynamic function (function can be defined during run-time) */
int32_t  bs_reg_dyn_fn(TBASIC_CTX *ctx, TCode code, char* name);

/* Get variable by name; Accept array name like ARRAY(1); ARRAY(0) return array size */
int32_t bs_get_var(TBASIC_CTX *ctx, char* name, TValue* val);

/* Set variable by name; Accept array name like ARRAY(1); ARRAY(0) return array size */
int32_t bs_set_var(TBASIC_CTX *ctx, char* name, TValue val);

/* Convert TData to string; str should by allocated by user */
int32_t  bs_val2str(TBASIC_CTX *ctx, char* str, TData val);

#if (ENABLE_DASM == ON)
/* Disassemble p-code command located by specified addr; asml should by allocated by user */
int32_t  bs_dasm(TBASIC_CTX *ctx, int32_t addr, char* asml);
#endif 


#if (ENABLE_COMPILER == ON)
/* Allocate memory from heap and initialize TBASIC_CTX */
int32_t bs_init_alloc(
	TBASIC_CTX *ctx,
	TFormatHook fmthook,	// callback for format string function (like vsprintf()); must be defined.
	TPutchHook putchook,	// callback for output char to console (like putchar()); must be defined.
	TErrorHook errhook,	// callback for error processing; can be NULL
	void* user_ptr);

/* Return expressions count separated by comma */
int32_t bs_get_nparam(TBASIC_CTX *ctx);

/* Parse n expressions, separated by comma */
void	bs_explist(TBASIC_CTX *ctx, int32_t n);

/* Insert one-arg instruction to program memory */
void	bs_inst1(TBASIC_CTX *ctx, TCode opcode);

/* Insert two-arg instruction to program memory */
void	bs_inst2(TBASIC_CTX *ctx, TCode opcode, int32_t x);

/* Parse expression */
int32_t	bs_expr(TBASIC_CTX *ctx);

/* Register new user-defined keyword */
int32_t bs_reg_keyword(TBASIC_CTX *ctx, TKwdHook khook);

/* Register new user-defined static function (function(s) must be defined during compile and run in same order!) */
int32_t bs_reg_func(TBASIC_CTX *ctx, TFunHook fhook);

/* Compile Basic source to p-code */
int32_t bs_compile(TBASIC_CTX *ctx, char *bsrc);

/* Export p-code to image; image buffer allocated inside and should be freeing by user  */
int32_t bs_export_pcode(TBASIC_CTX *ctx, void**	pcode, int32_t* pcode_sz);

/* Generate compile listing and report. see LST_PRINT_XXX */
int32_t bs_listing(TBASIC_CTX *ctx, char *bsrc, int32_t level, FILE* lst);

#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* _BASIC_H_ */
