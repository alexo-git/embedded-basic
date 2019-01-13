/*
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


#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "basic.h"

#ifdef __BORLANDC__
#define strtof				strtod
#define fmodf				fmod
#define ceilf				ceil
#define log2f				log2
#define logf				log
#define floorf				floor
#define sqrtf				sqrt
#define expf				exp
#define asinf				asin
#define acosf				acos
#define atan2f				atan2
#define atanf				atan
#define sinf				sin
#define cosf				cos
#define tanf				tan
#define roundf(x) 			(x<0?ceil((x)-0.5):floor((x)+0.5))

/* Best possible approximation of log(2) as a 'float'.  */
#define LOG2 0.693147180559945309417232121458176568075f

/* Best possible approximation of 1/log(2) as a 'float'.  */
#define LOG2_INVERSE 1.44269504088896340735992468100189213743f

/* sqrt(0.5).  */
#define SQRT_HALF 0.707106781186547524400844362104849039284f

float fabsf (float x)
{
	return fabs(x);
}

float
log2f (float x)
{
  if (x <= 0.0f)
    {
      if (x == 0.0f)
        /* Return -Infinity.  */
        return 1/0;
      else
        {
          /* Return NaN.  */
		  return 0.0f / 0.0f;
        }
    }

  /* Decompose x into
       x = 2^e * y
     where
       e is an integer,
       1/2 < y < 2.
     Then log2(x) = e + log2(y) = e + log(y)/log(2).  */
  {
    int e;
    float y;

    y = frexp (x, &e);
    if (y < SQRT_HALF)
      {
        y = 2.0f * y;
        e = e - 1;
      }

    return (float) e + logf (y) * LOG2_INVERSE;
  }
}

#endif


#define A					ctx->sp[1].v					/* LEFT OPERAND */
#define B					ctx->sp[0].v					/* RIGHT OPERAND */
#define PCV					(ctx->prg[(ctx->pc)++])			/* GET IMMEDIATE */
#define SP					(ctx->sp)
#define STEP				return 1						/* CONTINUE RUNNING */
#define SUB(V,N)			(*(ctx->sub + (V)*ctx->sloc_sz + (N)))
#define LOC(N)				ctx->var[SUB(v, N+2)].v		/* SUBROUTINE LOCAL */

/* P-CODE SIGNATURE */
const char bs_fsign[8] = { 'B','A','S', '1', 0, 0, ENABLE_MATH, DATATYPE };

/* RUN-TIME ERROR MESSAGE */
char	*msg_run_time[] =
{
	"SUCCESS",
	"ARRAY BOUNDS ERROR",
	"DIM SEGMENT OVERFLOW",
	"STACK OVERFLOW",
	"MATH OVERFLOW",
	"DIVISION BY ZERO",
	"MODULUS OF ZERO",
	"CODE SEGMENT OVERFLOW",
	"INVALID OPCODE",
	"UNDEFINED DYNAMIC FUNCTION",
	"DYNAMIC FUNCTION RUN ERROR"
};

#if (ENABLE_COMPILER == ON)

char	*msg_compile_time[] =
{
	"SUCCESS",
	"BAD TOKEN",
	"SYNTAX ERROR",
	"BAD SUB/ARG COUNT",
	"BAD EXPRESSION",
	"SUB MUST BE COMPILED",
	"BAD STATEMENT",
	"TOKENS AFTER STATEMENT",
	"VALUE OUT OF RANGE"
};

static void bs_emit_opcode(TBASIC_CTX *ctx, TCode opcode, int32_t len);
static void bs_emit_imm(TBASIC_CTX *ctx, int32_t imm);
static int32_t  bs_find(TBASIC_CTX *ctx, char *var);
static int32_t  bs_read(TBASIC_CTX *ctx);
static void bs_base(TBASIC_CTX *ctx);
static int32_t  bs_want(TBASIC_CTX *ctx, int32_t type);
static void bs_need(TBASIC_CTX *ctx, int32_t type);
static void bs_stmt(TBASIC_CTX *ctx);

enum
{
	NAME = 1, NUMBER, STRING, LP, RP, COMMA, ADD, SUBS, MUL, DIV, MOD,
	EQ, LT, GT, NE, LE, GE, AND, OR, FORMAT, SUB, END, RETURN, LOCAL,
	WHILE, FOR, TO, IF, ELSE, THEN, DIM, UBOUND, BYE
};

char	*kwd[] =
{
	"AND","OR","FORMAT","SUB","END","RETURN","LOCAL","WHILE",
	"FOR","TO","IF","ELSE","THEN","DIM","UBOUND","BYE",0
};
#endif

static int32_t bs_printf(TBASIC_CTX *ctx, const char *fmt, ...);
static int32_t bs_sprintf(TBASIC_CTX *ctx, char *str, const char *fmt, ...);

#if (DATATYPE == TYPE_FX16Q16)
static void bs_fx16tostr(fix16_t var, char *buf, int32_t decimals);
static fix16_t bs_strtofx16(char *buf, char** endptr);
#endif
void bs_err(TBASIC_CTX *ctx, int32_t nerr);

static int32_t BYE_(TBASIC_CTX *ctx)
{
	longjmp(ctx->trap, EXIT_BYE);
}

static int32_t NUMBER_(TBASIC_CTX *ctx)
{
	TValue val;
	val.i = PCV;
	(*--SP).v = val.v;
	STEP;
}

static int32_t STRING_(TBASIC_CTX *ctx)  // same as NUMBER_
{
	TValue val;
	val.p = (void*)((ssize_t)PCV);
	(*--SP).p = val.p;
	STEP;
}

static int32_t LOAD_(TBASIC_CTX *ctx)
{
	(*--SP).v = ctx->var[PCV].v;
	STEP;
}

static int32_t STORE_(TBASIC_CTX *ctx)
{
	ctx->var[PCV].v = (*SP++).v;
	STEP;
}

static int32_t ECHO_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	bs_printf(ctx, "%g\n", (*SP++).v);
#elif (DATATYPE == TYPE_INT32)
	bs_printf(ctx, "%d\n", (*SP++).v);
#elif (DATATYPE == TYPE_FX16Q16)
	{
		char sval[16];
		bs_fx16tostr((*SP++).v, sval, 12);
		bs_printf(ctx, "%s", sval);
	}
#endif
	STEP;
}

static int32_t FORMAT_(TBASIC_CTX *ctx)
{
	char* v;
	TValue* ap;
	int32_t  n = PCV;

#if (ENABLE_CHECK_STACK == ON)
	if (ctx->sp + n <= (ctx->stk))
		ctx->pc--, bs_err(ctx, ERR_STACK_OVERFLOW);
#endif

	ap = (TValue*)((SP += n) - 1);
	for (v = ctx->stab + (*SP++).i; *v; v++)
		if (v[0] == '%' && v[1] == '%')
			v++, ctx->putchook('%');
		else if (v[0] == '%')
#if (DATATYPE == TYPE_FLOAT32)
			bs_printf(ctx, "%g", (*ap--).v);
#elif (DATATYPE == TYPE_INT32)
			bs_printf(ctx, "%d", (*ap--).v);
#elif (DATATYPE == TYPE_FX16Q16)
	{
		char sval[16];
		bs_fx16tostr((*ap--).v, sval, 12);
		bs_printf(ctx, "%s", sval);
	}
#endif
		else if (v[0] == '$')
			bs_printf(ctx, "%s", &ctx->stab[(*ap--).i]);
		else
			ctx->putchook(*v);
	ctx->putchook('\n');
	STEP;
}

static int32_t ADD_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FX16Q16)
	A = fix16_add(A, B);
	if (A == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#else
	A += B;
#endif
	SP++; STEP;
};

static int32_t SUBS_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FX16Q16)
	A = fix16_sub(A, B);
	if (A == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#else
	A -= B;
#endif
	SP++; STEP;
};

static int32_t MUL_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FX16Q16)
	A = fix16_mul(A, B);
	if (A == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#else
	A *= B;
#endif
	SP++; STEP;
};

static int32_t DIV_(TBASIC_CTX *ctx)
{
	if (!B) SP += 2, bs_err(ctx, ERR_DIV_ZERO);
#if (DATATYPE == TYPE_FX16Q16)
	A = fix16_div(A, B);
	if (A == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#else
	A /= B;
#endif
	SP++; STEP;
};

static int32_t MOD_(TBASIC_CTX *ctx)
{
	if (!B) SP += 2, bs_err(ctx, ERR_MOD_ZERO);
#if (DATATYPE == TYPE_FLOAT32)
	A = (TData)fmodf(A, B);
#elif (DATATYPE == TYPE_INT32)
	A %= B; SP++; STEP;
#elif (DATATYPE == TYPE_FX16Q16)
	A = fix16_mod(A, B);
	if (A == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);

#endif
	SP++; STEP;
}

static int32_t EQ_(TBASIC_CTX *ctx)
{
	A = (TData)((A<B) ? BS_TRUE : BS_FALSE); SP++; STEP;
};

static int32_t LT_(TBASIC_CTX *ctx)
{
	A = (TData)((A<B) ? BS_TRUE : BS_FALSE); SP++; STEP;
};

static int32_t GT_(TBASIC_CTX *ctx)
{
	A = (TData)((A>B) ? BS_TRUE : BS_FALSE); SP++; STEP;
};

static int32_t NE_(TBASIC_CTX *ctx)
{
	A = (TData)((A != B) ? BS_TRUE : BS_FALSE); SP++; STEP;
};

static int32_t LE_(TBASIC_CTX *ctx)
{
	A = (TData)((A <= B) ? BS_TRUE : BS_FALSE); SP++; STEP;
};

static int32_t GE_(TBASIC_CTX *ctx)
{
	A = (TData)((A >= B) ? BS_TRUE : BS_FALSE); SP++; STEP;
};

static int32_t AND_(TBASIC_CTX *ctx)
{
	if ((A != 0.0) && (B != 0.0))
		A = BS_TRUE;
	else
		A = BS_FALSE;

	SP++; STEP;
};

static int32_t OR_(TBASIC_CTX *ctx)
{
	if ((A != 0.0) || (B != 0.0))
		A = BS_TRUE;
	else
		A = 0;

	SP++; STEP;
};

static int32_t JMP_(TBASIC_CTX *ctx)
{
	ctx->pc = ctx->prg[ctx->pc];
	STEP;
}

static int32_t FALSE_(TBASIC_CTX *ctx)
{
	if ((*SP++).v)
		ctx->pc++;
	else
		ctx->pc = ctx->prg[ctx->pc];
	STEP;
}

static int32_t FOR_(TBASIC_CTX *ctx)
{
	int32_t pass;
	int32_t curr;

#if (DATATYPE == TYPE_FX16Q16)
	pass = fix16_to_int((*SP).v) + 1;
	curr = fix16_to_int(ctx->var[PCV].v);
#else
	pass = (int32_t)((*SP).v + 1);
	curr = (int32_t)(ctx->var[PCV].v);
#endif

	if (curr >= pass)
		ctx->pc = ctx->prg[ctx->pc], SP++;
	else
		ctx->pc++;
	STEP;
}

static int32_t NEXT_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FX16Q16)
	ctx->var[PCV].v += F16(1);
#else
	ctx->var[PCV].v++;
#endif
	STEP;
}

static int32_t CALL_DYN_(TBASIC_CTX *ctx)
{
	int32_t ix;
	uint32_t hash_fn = (ctx->prg[(ctx->pc)++]);
	uint32_t nparam = (ctx->prg[(ctx->pc)++]);

	// search for dynamic function and call
	for (ix = 0; ix < ctx->dynf_sz; ix++)
	{
		if (hash_fn == ctx->dynf_list[ix].name_hash)
		{
			(*--SP).i = nparam;
			if (!ctx->dynf_list[ix].fn(ctx))
				ctx->pc-=2, bs_err(ctx, ERR_DYN_FN);
			else
				STEP;
		}
	}

	ctx->pc--,	bs_err(ctx, ERR_UNDEFINED_FN);
	return 0; // kiss VC to ass
}

static int32_t CALL_(TBASIC_CTX *ctx)
{
	int32_t  v = PCV;
	int32_t	 n = SUB(v,1);
	TData  x;
	TValue* ap = (TValue*)SP;
	while (n--)
	{
		x = LOC(n);
		LOC(n) = (*ap).v;
		(*ap++).v = x;
	}
	for (n = SUB(v,1); n < SUB(v,0); n++)
		(*--SP).v = LOC(n);
	(*--SP).i = ctx->pc;
	ctx->pc = ctx->var[v].i;
	STEP;
}

static int32_t RETURN_(TBASIC_CTX *ctx)
{
	int32_t v = PCV, n = SUB(v, 0);
	ctx->pc = (*SP++).i;
	while (n--)
		LOC(n) = (*SP++).v;
	STEP;
}

static int32_t SETRET_(TBASIC_CTX *ctx)
{
	ctx->ret.v = (*SP++).v;
	STEP;
}

static int32_t RV_(TBASIC_CTX *ctx)
{
	(*--SP).v = ctx->ret.v;
	STEP;
}

static int32_t DROP_(TBASIC_CTX *ctx)
{
	SP += PCV;
	STEP;
}

static int32_t DIM_(TBASIC_CTX *ctx)
{
	TValue* mem;
	int32_t n;
	int32_t v = PCV;
#if (DATATYPE == TYPE_FLOAT32)
	n = (int32_t)roundf((*SP++).v);
#elif (DATATYPE == TYPE_INT32)
	n = (*SP++).i;
#elif (DATATYPE == TYPE_FX16Q16)
	n = fix16_to_int((*SP++).v);
#endif
	mem = (TValue*)ctx->dmem + ctx->dtop;
	ctx->dtop += n + 1;
	if (ctx->dtop >= ctx->dmem_sz)
		ctx->pc--, bs_err(ctx, ERR_DIM);
	mem[0].i = n;
	ctx->var[v].p = mem;
//printf("DIM_():: mem[0].i=%d, ctx->var[v].p=%p, mem=%p\n", mem[0].i, ctx->var[v].p, mem )	;
	STEP;
}

static TValue *bound(TBASIC_CTX *ctx, TValue mem, int32_t n)
{
	int arr_sz = ((TValue*)mem.p)[0].i;
//printf("bound()::  n=%d, mem.p = %p, sz=%d\n", n, mem.p, arr_sz);
	if (n < 1 || n > ((TValue*)mem.p)->i)
		ctx->pc--, bs_err(ctx, ERR_BOUNDS);
	return (TValue*)mem.p + n;
}

static int32_t LOADI_(TBASIC_CTX *ctx)
{
	int32_t i;
	TData v;
#if (DATATYPE == TYPE_FLOAT32)
	i = (int32_t)roundf((*SP++).v);
#elif (DATATYPE == TYPE_INT32)
	i = (*SP++).i;
#elif (DATATYPE == TYPE_FX16Q16)
	i = fix16_to_int((*SP++).v);
#endif
	v = bound(ctx, ctx->var[PCV], i)->v;
	(*--SP).v = v;
	STEP;
}

static int32_t STOREI_(TBASIC_CTX *ctx)
{
	TData	x = (*SP++).v;
	TData	i = (*SP++).v;
#if (DATATYPE == TYPE_FLOAT32)
	bound(ctx, ctx->var[PCV], (int32_t)roundf(i))->v = x;
#elif (DATATYPE == TYPE_INT32)
	bound(ctx, ctx->var[PCV], i)->i = x;
#elif (DATATYPE == TYPE_FX16Q16)
	bound(ctx, ctx->var[PCV], fix16_to_int(i))->v = x;
#endif
	STEP;
}

static int32_t UBOUND_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FX16Q16)
	int32_t sz = fix16_from_int(*((int32_t*)ctx->var[PCV].p));
#else
	int32_t sz = *((int32_t*)ctx->var[PCV].p);
#endif
	(*--SP).v = (TData)sz;
	STEP;
}

#if (ENABLE_MATH == ON)

static int32_t ABS_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = fabsf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_abs(B);
#endif
	STEP;
}

static int32_t FLOOR_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = floorf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_floor(B);
#endif
	STEP;
}

static int32_t CEIL_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = ceilf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_ceil(B);
#endif
	STEP;
}


static int32_t SQRT_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = sqrtf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_sqrt(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t SQ_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = (B)*(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_sq(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t EXP_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = expf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_exp(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t LOG_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = logf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_log(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t LOG2_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = log2f(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_log2(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t SIN_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = sinf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_sin(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t COS_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = cosf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_cos(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t TAN_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = tanf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_tan(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t ASIN_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = asinf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_asin(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t ACOS_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = acosf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_acos(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t ATAN_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = atanf(B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_atan(B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	STEP;
}

static int32_t ATAN2_(TBASIC_CTX *ctx)
{
#if (DATATYPE == TYPE_FLOAT32)
	B = atan2f(A,B);
#elif (DATATYPE == TYPE_FX16Q16)
	B = fix16_atan2(A, B);
	if (B == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
#endif
	SP++;  STEP;
}

#if (DATATYPE == TYPE_FLOAT32)
#define M_PI		3.1415926f
#define M_TWOPI		(2*M_PI)
#define M_180		180
#define M_360		360
#elif (DATATYPE == TYPE_FX16Q16)
#define M_PI		fix16_pi
#define M_TWOPI		(2*M_PI)
#define M_180		F16(180)
#define M_360		F16(360)
#endif

static TData amod(TBASIC_CTX *ctx, TData x, TData y)
{
	TData res;
	if (0 == y)
		return x;
#if (DATATYPE == TYPE_FLOAT32)
	res = x - y * floorf(x / y);
#elif (DATATYPE == TYPE_FX16Q16)
	TData divv = fix16_div(x, y);
	if (divv == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
	TData mulv = fix16_mul(y, fix16_floor(divv));
	if (divv == fix16_overflow)
		bs_err(ctx, ERR_MATH_OVERFLOW);
	res = fix16_sub(x, mulv);
#endif
	return res;
}


// wrap [rad] angle to [-PI..PI)
static int32_t ANGLE_WRAP_PI_(TBASIC_CTX *ctx)
{
	B = amod(ctx, B + M_PI, M_TWOPI) - M_PI;
	STEP;
}

// wrap [rad] angle to [0..TWO_PI)
static int32_t ANGLE_WRAP_TWOPI_(TBASIC_CTX *ctx)
{
	B = amod(ctx, B, M_TWOPI);
	STEP;
}

// wrap [deg] angle to [-180..180)
static int32_t ANGLE_WRAP_180_(TBASIC_CTX *ctx)
{
	B = amod(ctx, B + M_180, M_360) - M_180;
	STEP;
}

// wrap [deg] angle to [0..360)
static int32_t ANGLE_WRAP_360_(TBASIC_CTX *ctx)
{
	B = amod(ctx, B + M_180, M_360);
	STEP;
}

// calculate distance between x and y in degree
static int32_t ANGLE_DIFF_180_(TBASIC_CTX *ctx)
{
	TData arg;
	arg = amod(ctx, A - B, M_360);
	if (arg < 0)  arg = arg + M_360;
	if (arg > M_180) arg = arg - M_360;
	A = -arg;
	SP++;
	STEP;
}

#endif // (ENABLE_MATH == ON)


TCode optable[OPTSZ] =
{
	UBOUND_,STOREI_,LOADI_,DIM_,DROP_,RV_,SETRET_,RETURN_,CALL_,CALL_DYN_,
	NEXT_,FOR_,FALSE_,JMP_,OR_,AND_,GE_,LE_,NE_,GT_,LT_,EQ_, 
	MOD_,DIV_,MUL_,SUBS_,ADD_, FORMAT_,ECHO_,STORE_,LOAD_,NUMBER_, STRING_, BYE_,
#if (ENABLE_MATH == ON)
	ABS_, FLOOR_, CEIL_, SQRT_, SQ_, EXP_, LOG_, LOG2_, SIN_, COS_, TAN_, ASIN_, ACOS_, ATAN_, ATAN2_,
	ANGLE_WRAP_PI_, ANGLE_WRAP_TWOPI_, ANGLE_WRAP_180_, ANGLE_WRAP_360_, ANGLE_DIFF_180_,
#endif // ENABLE_MATH
	NULL
};

#if ((ENABLE_DASM == ON) || (ENABLE_COMPILER == ON))
char opname[OPTSZ][OPNSZ] =
{
	"UBOUND","STOREI","LOADI","DIM","DROP","RV","SETRET","RETURN","CALL","CALL_DYN",
	"NEXT","FOR","FALSE","JMP","OR","AND","GE","LE","NE","GT","LT","EQ",
	"MOD","DIV","MUL","SUBS","ADD","FORMAT","ECHO","STORE","LOAD","NUMBER","STRING","BYE",
#if (ENABLE_MATH == ON)
	"ABS", "FLOOR", "CEIL", "SQRT", "SQ", "EXP", "LOG", "LOG2", "SIN", "COS", "TAN", "ASIN", "ACOS", "ATAN", "ATAN2",
	"ANGLE_WRAP_PI", "ANGLE_WRAP_TWOPI", "ANGLE_WRAP_180", "ANGLE_WRAP_360", "ANGLE_DIFF_180",
#endif // ENABLE_MATH
	""
};
#endif

#if (ENABLE_COMPILER == ON)
int32_t opargcnt[OPTSZ] =
{
	2,2,2,2,2,1,1,2,2,3,
	2,3,2,2,1,1,1,1,1,1,1,1,
	1,1,1,1,1,2,1,2,2,2,2,1,
#if (ENABLE_MATH == ON)
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,
	1,1,1,1,2,
#endif // ENABLE_MATH
	0
};
#endif

#if (ENABLE_DASM == ON)

int32_t bs_dasm(TBASIC_CTX *ctx, int32_t addr, char* asml)
{
	char op[OPNSZ];
	int32_t oplen;
	int32_t opcode = GET_OPCODE(ctx->prg[addr]);
	int32_t nlen = 0;
	while (opname[nlen++][0]);
	if (opcode < nlen)
		bs_sprintf(ctx, op, "%s", opname[opcode]);
	else
		bs_sprintf(ctx, op, "?x%08X?", ctx->prg[addr]);

	oplen = GET_OPLEN(ctx->prg[addr]);
	switch (oplen)
	{
	case 1:	
		bs_sprintf(ctx, asml, "\t0x%08X\t%s", addr, op);
		break;
	case 2:	
		if (optable[opcode] == NUMBER_)
#if (DATATYPE == TYPE_FLOAT32)
			bs_sprintf(ctx, asml, "\t0x%08X\t%s\t%g;", addr, op, *(float*)&ctx->prg[addr + 1]);
#elif (DATATYPE == TYPE_INT32)
			bs_sprintf(ctx, asml, "\t0x%08X\t%s\t%d;", addr, op, *(int32_t*)&ctx->prg[addr + 1]);
#elif (DATATYPE == TYPE_FX16Q16)
		{
			char sval[16];
			bs_fx16tostr(ctx->prg[addr + 1], sval, 12);
			bs_sprintf(ctx, asml, "\t0x%08X\t%s\t%s;", addr, op, sval);
		}
#endif
		else if (optable[opcode] == STRING_)
			bs_sprintf(ctx, asml, "\t0x%08X\t%s\t'%s';", addr, op, ctx->stab + ctx->prg[addr + 1]);
		else
			bs_sprintf(ctx, asml, "\t0x%08X\t%s\t0x%08X;", addr, op, ctx->prg[addr + 1]);
		break;
	case 3:	
		bs_sprintf(ctx, asml, "\t0x%08X\t%s\t0x%08X,0x%08X", addr, op, ctx->prg[addr + 1], ctx->prg[addr + 2]);
		break;
	default: 
		bs_sprintf(ctx, asml, "\t0x%08X\t?x%08X?", addr, GET_OPCODE(ctx->prg[addr]));
	}

	return oplen;
}
#endif

int32_t bs_reg_opcode(TBASIC_CTX *ctx, TCode code, char* name)
{
	int32_t n = 0;
	while ((n<OPTSZ - 1) && optable[n])
	{
		if (optable[n] == code)
			break;		// already registered

		n++;
	}

	if (n < OPTSZ - 1)
	{
		optable[n] = code;
		optable[n + 1] = NULL;

#if (ENABLE_DASM == ON)
		strncpy(opname[n], name, OPNSZ);
		opname[n + 1][0] = 0;
#endif
		return EXIT_SUCCESS;
	}
	return -EXIT_ERROR;
}

#if (ENABLE_COMPILER == ON)

TCode bin[] = { ADD_,SUBS_,MUL_,DIV_,MOD_,EQ_,LT_,GT_, NE_,LE_,GE_,AND_,OR_ };

#define BIN(NAME,LO,HI,ELEM)  int32_t NAME(TBASIC_CTX *ctx) { TCode o; \
	ELEM(ctx); \
	while (bs_want(ctx, 0), LO<=ctx->tok && ctx->tok<=HI) \
		o=bin[ctx->tok-ADD], bs_read(ctx), ELEM(ctx), bs_inst1(ctx, o); \
	return 0; }

BIN(factor, MUL, MOD, bs_base)
BIN(addition, ADD, SUBS, factor)
BIN(relation, EQ, GE, addition)
BIN(bs_expr, AND, OR, relation)

#endif

void bs_err(TBASIC_CTX *ctx, int32_t nerr)
{
	if (ctx->errhook)
	{
		ctx->errhook(ctx,
			nerr,
			GET_LINEn(ctx->prg[ctx->pc - 1]),
			ctx->pc,
			msg_run_time[nerr]
			);
	}
	ctx->nerr = nerr;
	longjmp(ctx->trap, EXIT_ERROR_RUNTIME);
}

#if (ENABLE_COMPILER == ON)
static void bs_bad(TBASIC_CTX *ctx, int32_t nerr)
{
	if (ctx->errhook)
	{
		ctx->errhook(ctx,
			nerr,
			ctx->lnum,
			ctx->cpc,
			msg_compile_time[nerr]
		);
	}

	ctx->nerr = nerr;
	longjmp(ctx->trap, EXIT_ERROR_COMPILE);
}
#endif

int32_t bs_last_error(TBASIC_CTX *ctx, int32_t* lnum, int32_t* addr, char** msg)
{
#if (ENABLE_COMPILER == ON)
	if (ctx->fcompile)
	{
		if (lnum) 
			*lnum = ctx->lnum;
		if (addr) 
			*addr = ctx->cpc;
		if (msg) 
			*msg = msg_compile_time[ctx->nerr];
	}
	else
#endif
	{
		if (lnum && ctx->nerr)
		{
			//int32_t cmdlen = GET_OPLEN(ctx->prg[ctx->pc]);
			*lnum = GET_LINEn(ctx->prg[ctx->pc - 1]);
		}
		if (addr) 
			*addr = ctx->pc;
		if (msg) 
			*msg = msg_run_time[ctx->nerr];
	}

	return ctx->nerr;
}

int32_t bs_init(
	TBASIC_CTX *ctx, 
	int32_t* prg, int32_t prg_sz,
	TValue* stk, int32_t stk_sz,
	TValue* var,	int32_t var_sz,
	TValue* dmem, int32_t dmem_sz,
	int32_t* sub,	int32_t sloc_sz,
	char* stab, int32_t stab_sz,
	TFormatHook fmthook, 
	TPutchHook putchook, 
	TErrorHook errhook,
	void* user_ptr)
{
	memset(ctx, 0, sizeof(TBASIC_CTX));
	ctx->prg = prg; ctx->prg_sz = prg_sz;
	ctx->stk = stk; ctx->stk_sz = stk_sz;
	ctx->var = var; ctx->var_sz = var_sz;
	ctx->dmem = dmem; ctx->dmem_sz = dmem_sz;
	ctx->sub = sub; ctx->sloc_sz = sloc_sz;
	ctx->stab = stab; ctx->stab_sz = stab_sz;
	ctx->errhook = errhook;
	ctx->fmthook = fmthook;
	ctx->putchook = putchook;

	ctx->pc = 0;
	ctx->sp = ctx->stk + ctx->stk_sz;
	ctx->dtop = 0;

#if (ENABLE_COMPILER == ON)
	ctx->csp = ctx->cstk + STKSZ;
	ctx->stabp = ctx->stab;
	ctx->fcompile = 1;
#endif
	ctx->user_ptr = user_ptr;
	return EXIT_SUCCESS;
}

#if (ENABLE_COMPILER == ON)

int32_t bs_init_alloc(
	TBASIC_CTX *ctx,
	TFormatHook fmthook,
	TPutchHook putchook,
	TErrorHook errhook,
	void* user_ptr)
{
	int32_t* prg = NULL;
	TValue* stk = NULL;
	TValue* var = NULL;
	TValue* dmem = NULL;
	int32_t* sub = NULL;
	char* stab = NULL;

	int32_t err = EXIT_ERROR_ALLOC;

	prg = (int32_t*)calloc(1, PRGSZ); 
	if (prg == NULL) goto _EXIT;

	stk = (TValue*)calloc(1, STKSZ * sizeof(TValue));
	if (stk == NULL) goto _EXIT;

	var = (TValue*)calloc(1, VARS * sizeof(TValue));
	if (var == NULL) goto _EXIT;

	dmem = (TValue*)calloc(1, DIMSZ * sizeof(TValue));
	if (dmem == NULL) goto _EXIT;

	sub = (int32_t*)calloc(1, VARS * LOCS * sizeof(int32_t));
	if (sub == NULL) goto _EXIT;

	stab = (char*)calloc(1, STRSZ);
	if (stab == NULL) goto _EXIT;

	err = bs_init(ctx,
		prg, PRGSZ,
		stk, STKSZ,
		var, VARS,
		dmem, DIMSZ,
		sub, LOCS,
		stab, STRSZ,
		fmthook,
		putchook,
		errhook,
		user_ptr);

_EXIT:

	if (err != EXIT_SUCCESS)
	{
		free(prg);
		free(stk);
		free(var);
		free(dmem);
		free(sub);
		free(stab);
	}

	return err;
}
#endif


int32_t bs_init_load(
	TBASIC_CTX *ctx,
	void* pcode,
	TFormatHook fmthook,
	TPutchHook putchook,
	TErrorHook errhook,
	void* user_ptr
	)
{
	#define CPY_TO(to, sz)	for (i=0; i<(int32_t)sz; i++) {((char*)to)[i] = *psrc++;}

	const char fsign[8];
	int32_t  i, j;
	int32_t  code_len, cstr_len, var_len, sub_len, sub_deep, stk_len, dmem_len, vhash_len, vname_len;
	int32_t  err;
	char* psrc = (char*)pcode;

	memset(ctx, 0, sizeof(TBASIC_CTX));

	CPY_TO(&fsign, sizeof(fsign));
	if (memcmp(bs_fsign, fsign, sizeof(fsign)) != 0)
	{
		err = EXIT_ERROR_VERSION;
		goto _EXIT_ERROR;
	}

	CPY_TO(&code_len, sizeof(code_len));  // read p-code segment size
	CPY_TO(&cstr_len, sizeof(cstr_len));  // read string segment size
	CPY_TO(&var_len, sizeof(var_len));  // read string segment size
	CPY_TO(&sub_len, sizeof(sub_len));  // read sub-var segment size
	CPY_TO(&sub_deep, sizeof(sub_deep));  // read sub-loc segment size
	CPY_TO(&stk_len, sizeof(stk_len));  // read stack segment size
	CPY_TO(&dmem_len, sizeof(dmem_len));  // read dim segment size
	CPY_TO(&vhash_len, sizeof(vhash_len));  // read hash of var name segment size
	CPY_TO(&vname_len, sizeof(vname_len));  // read exported var name segment size

	err = EXIT_ERROR_ALLOC;
	ctx->prg = calloc(1, code_len * sizeof(int32_t));
	if (ctx->prg == NULL) goto _EXIT_ERROR;
	ctx->prg_sz = code_len;

	ctx->var = calloc(1, var_len * sizeof(TValue)); 
	if (ctx->var == NULL) goto _EXIT_ERROR;
	ctx->var_sz = var_len;

	ctx->sub = calloc(1, sub_len * sub_deep * sizeof(int32_t)); 
	if (ctx->sub == NULL) goto _EXIT_ERROR;
	ctx->sloc_sz = sub_deep;

	ctx->stab = calloc(1, cstr_len); 
	if (ctx->stab == NULL) goto _EXIT_ERROR;
	ctx->stab_sz = cstr_len;

	ctx->stk = calloc(1, stk_len * sizeof(TValue));
	if (ctx->stk == NULL) goto _EXIT_ERROR;
	ctx->stk_sz = stk_len;

	ctx->dmem = calloc(1, dmem_len * sizeof(TValue));
	if (ctx->dmem == NULL) goto _EXIT_ERROR;
	ctx->dmem_sz = dmem_len;

	ctx->vhash = calloc(1, vhash_len * sizeof(uint32_t));
	if (ctx->vhash == NULL) goto _EXIT_ERROR;
	ctx->vhash_sz = vhash_len;

	CPY_TO(ctx->prg, code_len * (int32_t)sizeof(int32_t));		// read p-code segment
	CPY_TO(ctx->var, var_len * (int32_t)sizeof(TValue));	// read value segment

	// read local var segment
	for (j = 0; j<sub_len; j++)
		CPY_TO(&SUB(j, 0), sub_deep * (int32_t)sizeof(int32_t));

	CPY_TO(ctx->vhash, vhash_len * (int32_t)sizeof(uint32_t));	// read var name hash table
	CPY_TO(ctx->stab, cstr_len);	// read string segment


	ctx->vname = calloc(1, vname_len * sizeof(uint32_t));
	if (ctx->vname == NULL) goto _EXIT_ERROR;
	ctx->vname_sz = vname_len;
	CPY_TO(ctx->vname, vname_len * (int32_t)sizeof(uint32_t));	// read var name export table


	ctx->errhook = errhook;
	ctx->fmthook = fmthook;
	ctx->putchook = putchook;
	
	ctx->sp = ctx->stk + ctx->stk_sz;

	ctx->user_ptr = user_ptr;
	return EXIT_SUCCESS;

_EXIT_ERROR:
	free(ctx->prg);
	free(ctx->stk);
	free(ctx->var);
	free(ctx->dmem);
	free(ctx->sub);
	free(ctx->stab);
	free(ctx->vhash);
	free(ctx->vname);
	free(ctx->dynf_list);
	return err;
}

int32_t bs_free(TBASIC_CTX *ctx)
{
	free(ctx->prg);
	free(ctx->stk);
	free(ctx->var);
	free(ctx->dmem);
	free(ctx->sub);
	free(ctx->stab);
	free(ctx->vhash);
	free(ctx->dynf_list);
	return EXIT_SUCCESS;
}

static int32_t bs_printf(TBASIC_CTX *ctx, const char *fmt, ...)
{
	va_list ap;
	int32_t retval = -1;
	char str[FMTSZ];
	char* pstr = str;

	if (ctx->fmthook && ctx->putchook)
	{
		va_start(ap, fmt);
		retval = ctx->fmthook(str, fmt, ap);
		while(*pstr)
			ctx->putchook(*pstr++);
		va_end(ap);
	}

	return retval;
}

static int32_t bs_sprintf(TBASIC_CTX *ctx, char *str, const char *fmt, ...)
{
	va_list ap;
	int32_t retval = -1;

	if (ctx->fmthook)
	{
		va_start(ap, fmt);
		retval = ctx->fmthook(str, fmt, ap);
		va_end(ap);
	}

	return retval;
}

/* Actually, regular CRC32 */
static uint32_t bs_hash(char *message)
{
	int i, j, crc;
	uint32_t byte, c;
	const uint32_t g0 = 0xEDB88320, g1 = g0 >> 1, g2 = g0 >> 2, g3 = g0 >> 3;

	i = 0;
	crc = 0xFFFFFFFF;
	while (message[i] != 0)
	{
		byte = message[i];                // Get next byte.
		crc = crc ^ byte;
		for (j = 1; j >= 0; j--)
		{        // Do two times.
			c = ((crc << 31 >> 31) & g3) ^ ((crc << 30 >> 31) & g2) ^
				((crc << 29 >> 31) & g1) ^ ((crc << 28 >> 31) & g0);
			crc = ((unsigned)crc >> 4) ^ c;
		}
		i = i + 1;
	}
	return ~crc;
}


#if (ENABLE_COMPILER == ON)

int32_t bs_reg_keyword(TBASIC_CTX *ctx, TKwdHook khook)
{
	ctx->kwdhook = khook;
	return EXIT_SUCCESS;
}

int32_t bs_reg_func(TBASIC_CTX *ctx, TFunHook fhook)
{
	ctx->funhook = fhook;
	return EXIT_SUCCESS;
}

#endif


#if (DATATYPE == TYPE_FX16Q16)

static const uint32_t scales[8] = {
	/* 5 decimals is enough for full fix16_t precision */
	1, 10, 100, 1000, 10000, 100000, 100000, 100000
};

static char *itoa_loop(char *buf, uint32_t scale, uint32_t var, int32_t skip)
{
	while (scale)
	{
		unsigned digit = (var / scale);

		if (!skip || digit || scale == 1)
		{
			skip = 0;
			*buf++ = '0' + digit;
			var %= scale;
		}

		scale /= 10;
	}
	return buf;
}

static void bs_fx16tostr(fix16_t var, char *buf, int32_t decimals)
{
	uint32_t uvalue = (var >= 0) ? var : -var;
	if (var < 0)
		*buf++ = '-';

	/* Separate the integer and decimal parts of the value */
	unsigned intpart = uvalue >> 16;
	uint32_t fracpart = uvalue & 0xFFFF;
	uint32_t scale = scales[decimals & 7];
	fracpart = fix16_mul(fracpart, scale);

	if (fracpart >= scale)
	{
		/* Handle carry from decimal part */
		intpart++;
		fracpart -= scale;
	}

	/* Format integer part */
	buf = itoa_loop(buf, 10000, intpart, 1);

	/* Format decimal part (if any) */
	if (scale != 1 && fracpart)
	{
		*buf++ = '.';
		buf = itoa_loop(buf, scale / 10, fracpart, 0);
	}

	*buf = '\0';
}

static fix16_t bs_strtofx16(char *buf, char** endptr)
{
	while (isspace(*buf))
		buf++;

	/* Decode the sign */
	int32_t  negative = (*buf == '-');
	if (*buf == '+' || *buf == '-')
		buf++;

	/* Decode the integer part */
	uint32_t intpart = 0;
	int32_t count = 0;
	while (isdigit(*buf))
	{
		intpart *= 10;
		intpart += *buf++ - '0';
		count++;
	}

	if (count == 0 || count > 5
		|| intpart > 32768 || (!negative && intpart > 32767))
	{
		*endptr = buf;
		return fix16_overflow;
	}

	fix16_t var = intpart << 16;

	/* Decode the decimal part */
	if (*buf == '.')
	{
		buf++;

		uint32_t fracpart = 0;
		uint32_t scale = 1;
		while (isdigit(*buf) && scale < 100000)
		{
			scale *= 10;
			fracpart *= 10;
			fracpart += *buf++ - '0';
		}

		var += fix16_div(fracpart, scale);
	}

	while (isdigit(*buf) || isspace(*buf))
		buf++;

	*endptr = buf;
	return negative ? -var : var;
}
#endif

#if (ENABLE_COMPILER == ON)

void bs_explist(TBASIC_CTX *ctx, int32_t n)
{
	while (n-->1) bs_expr(ctx), bs_need(ctx, COMMA); bs_expr(ctx);
}

static void bs_emit_opcode(TBASIC_CTX *ctx, TCode opcode, int32_t len)
{
	int32_t n = 0;
	while (optable[n] && (opcode != optable[n])) n++;
	if (optable[n])
		if (ctx->cpc < ctx->prg_sz)
			ctx->prg[ctx->cpc++] = ((ctx->lnum << 16) & 0xffff0000) | ((len << 8) & 0x0000ff00) | n;	// bits 0..7 - opcode index, 8..15 - opcode len, bits 16..31 - line num
		else
			bs_bad(ctx, ERR_CODE_OVERFLOW);
	else
		bs_bad(ctx, ERR_INVALID_OPCODE);
}

static void bs_emit_imm(TBASIC_CTX *ctx, int32_t imm)
{
	if (ctx->cpc < ctx->prg_sz)
		ctx->prg[ctx->cpc++] = imm;
	else
		bs_bad(ctx, ERR_CODE_OVERFLOW);
}

void bs_inst1(TBASIC_CTX *ctx, TCode opcode)
{
	bs_emit_opcode(ctx, opcode, 1);
}

void bs_inst2(TBASIC_CTX *ctx, TCode opcode, int32_t x)
{
	bs_emit_opcode(ctx, opcode, 2);
	bs_emit_imm(ctx, x);
}

void bs_inst3(TBASIC_CTX *ctx, TCode opcode, int32_t x, int32_t y)
{
	bs_emit_opcode(ctx, opcode, 3);
	bs_emit_imm(ctx, x);
	bs_emit_imm(ctx, y);
}

static int32_t bs_find(TBASIC_CTX *ctx, char *var)
{
	int32_t i;
	for (i = 0; i < ctx->nvar && strcmp(var, ctx->name[i]); i++);
	if (i == ctx->nvar)
		strncpy(ctx->name[ctx->nvar++], var, SYMSZ);
	return i;
}

static int32_t bs_read(TBASIC_CTX *ctx)
{ /* READ TOKEN */
	char *p, *d, **k, *pun = "(),+-*/%=<>", *dub = "<><==>";
	if (ctx->ungot)
		return ctx->ungot = 0, ctx->tok; /* UNGOT PREVIOUS */
	while (isspace(*ctx->lp))
		ctx->lp++; /* SKIP SPACE */
	if (!*ctx->lp || *ctx->lp == '#' || *ctx->lp == ';')
		return ctx->tok = 0; /* END OF LINE */
	if (isdigit(*ctx->lp)) /* NUMBER */
	{
		TData v;
#if (DATATYPE == TYPE_FLOAT32)
		v = (TData)strtof(ctx->lp, &ctx->lp);
#elif (DATATYPE == TYPE_INT32)
		v = strtol(ctx->lp, &ctx->lp, 0);
#elif (DATATYPE == TYPE_FX16Q16)
		v = bs_strtofx16(ctx->lp, &ctx->lp);
		if (v == fix16_overflow)
			bs_bad(ctx, ERR_OUT_OF_RANGE);
#endif
		ctx->tokv.v = v;
		return ctx->tok = NUMBER;
	}
	if ((p = strchr(pun, *ctx->lp)) && ctx->lp++)
	{ /* PUNCTUATION */
		for (d = dub; *d && strncmp(d, ctx->lp - 1, 2); d += 2);
		if (!*d)
			return ctx->tok = (p - pun) + LP;
		return ctx->lp++, ctx->tok = (d - dub) / 2 + NE;
	}
	else if (isalpha(*ctx->lp) || (*ctx->lp == '_'))
	{ /* IDENTIFIER */
		for (p = ctx->tokn; (isalnum(*ctx->lp) || (*ctx->lp == '_'));)
			*p++ = *ctx->lp++;   //*p++ = toupper(*ctx->lp++);
		for (*p = 0, k = kwd; *k && strcmp(ctx->tokn, *k); k++);
		if (*k)
			return ctx->tok = (k - kwd) + AND;
		return ctx->tokv.i = bs_find(ctx, ctx->tokn), ctx->tok = NAME;
	}
	else if (*ctx->lp == '"' && ctx->lp++)
	{ /* STRING */
		for (p = ctx->stabp; *ctx->lp && *ctx->lp != '"';)
			*ctx->stabp++ = *ctx->lp++;
		*ctx->stabp++ = 0;
		ctx->lp++;
		ctx->tokv.i = p - ctx->stab;
		return ctx->tok = STRING;
	}
	else
		bs_bad(ctx, ERR_BAD_TOKEN);
	return 0;
}

static int32_t bs_want(TBASIC_CTX *ctx, int32_t type)
{
	return !(ctx->ungot = bs_read(ctx) != type);
}

static void bs_need(TBASIC_CTX *ctx, int32_t type)
{
	if (!bs_want(ctx, type))
		bs_bad(ctx, ERR_SYNTAX);
}

#if (ENABLE_MATH == ON)

static TCode find_mathfn(char* name, int32_t argn)
{
	int32_t ix;
	for (ix = 0; ix < OPTSZ; ix++)
		if ((strcmp(opname[ix], name) == 0) &&
			(opargcnt[ix] == argn))
			break;
	return (ix== OPTSZ)?(NULL):((TCode)optable[ix]);
}

static int32_t bs_parse_mathfn(TBASIC_CTX *ctx, int32_t var, int32_t n)
{
	TCode fn;
	if ((fn = find_mathfn(ctx->name[var], n)) != NULL)
	{
		ctx->mode[var] = 3;
		bs_inst1(ctx, fn);
	}
	else
		return 0;

	return 1;
}

#endif


#define LIST(BODY) if (!bs_want(ctx, 0)) do {BODY;} while (bs_want(ctx, COMMA))

static void bs_base(TBASIC_CTX *ctx)
{ /* BASIC EXPRESSION */
	int32_t neg = bs_want(ctx, SUBS) ? (bs_inst2(ctx, NUMBER_, 0), 1) : 0;
	if (bs_want(ctx, NUMBER))
		bs_inst2(ctx, NUMBER_, ctx->tokv.i);
	else if (bs_want(ctx, STRING))
		bs_inst2(ctx, STRING_, (int32_t)(ctx->tokv.i));
	else if (bs_want(ctx, NAME))
	{
		int32_t var = ctx->tokv.i;
		if (bs_want(ctx, LP))
			if (ctx->mode[var] == 1) /* DIM */
				bs_expr(ctx), bs_need(ctx, RP), bs_inst2(ctx, LOADI_, var);
			else
			{
				int32_t n = 0;
				LIST(if (ctx->tok == RP) break; bs_expr(ctx); n++);
				bs_need(ctx, RP);

#if (ENABLE_MATH == ON)
				if (!bs_parse_mathfn(ctx, var, n))
#endif

				if (!ctx->funhook || !ctx->funhook(ctx, ctx->name[var], n))
				{
					if (ctx->mode[var] != 2 || n != SUB(var, 1))
					{
						bs_inst3(ctx, CALL_DYN_, bs_hash(ctx->name[var]), n);
						ctx->mode[var] = 3;
						//bs_bad(ctx, ERR_COUNT);
					}
					else
					{
						bs_inst2(ctx, CALL_, var);
						bs_inst1(ctx, RV_);
					}
				}
			}
		else
			bs_inst2(ctx, LOAD_, var);
	}
	else if (bs_want(ctx, LP))
		bs_expr(ctx), bs_need(ctx, RP);
	else if (bs_want(ctx, UBOUND))
		bs_need(ctx, LP), bs_need(ctx, NAME), bs_need(ctx, RP), bs_inst2(ctx, UBOUND_, ctx->tokv.i);
	else
		bs_bad(ctx, ERR_EXPRESSION);
	if (neg)
		bs_inst1(ctx, SUBS_); /* NEGATE */
}

int32_t bs_get_nparam(TBASIC_CTX *ctx)
{
	int32_t n = 0;
	LIST(bs_expr(ctx); n++);
	return n;
}


static void bs_stmt(TBASIC_CTX *ctx)
{ /* STATEMENT */
	int32_t n, var;
	switch (bs_read(ctx))
	{
	case FORMAT:
		bs_need(ctx, STRING), bs_inst2(ctx, STRING_, ctx->tokv.i);
		n = 0;
		if (bs_want(ctx, COMMA))
			LIST(bs_expr(ctx); n++);
		bs_inst2(ctx, FORMAT_, n);
		break;
	case SUB: /* CSTK: {SUB,INDEX,JMP} */
		if (!ctx->fcompile)
			bs_bad(ctx, ERR_COMP_MODE);
		ctx->fcompile++; /* MUST BALANCE WITH END */
		bs_need(ctx, NAME), ctx->mode[ctx->cursub = var = ctx->tokv.i] = 2; /* SUB NAME */
		n = 0;
		LIST(bs_need(ctx, NAME); SUB(var, n++ + 2) = ctx->tokv.i); /* PARAMS */
		*--ctx->csp = ctx->cpc + 1, bs_inst2(ctx, JMP_, 0); /* JUMP OVER CODE */
		SUB(var, 0) = SUB(var, 1) = n; /* LOCAL=PARAM COUNT */
		ctx->var[var].i = ctx->cpc; /* ADDRESS */
		*--ctx->csp = var, *--ctx->csp = SUB; /* FOR "END" CLAUSE */
		break;
	case LOCAL:
		LIST(bs_need(ctx, NAME); SUB(ctx->cursub, SUB(ctx->cursub, 0)++ + 2) = ctx->tokv.i;);
		break;
	case RETURN:
		if (ctx->temp)
			bs_inst2(ctx, DROP_, ctx->temp);
		if (!bs_want(ctx, 0))
			bs_expr(ctx), bs_inst1(ctx, SETRET_);
		bs_inst2(ctx, RETURN_, ctx->cursub);
		break;
	case WHILE: /* CSTK: {WHILE,TEST-FALSE,TOP} */
		ctx->fcompile++; /* BODY IS COMPILED */
		*--ctx->csp = ctx->cpc, bs_expr(ctx);
		*--ctx->csp = ctx->cpc + 1, *--ctx->csp = WHILE, bs_inst2(ctx, FALSE_, 0);
		break;
	case FOR: /* CSTK: {FOR,TEST-FALSE,I,TOP}; STK:{HI} */
		ctx->fcompile++; /* BODY IS COMPILED */
		bs_need(ctx, NAME), var = ctx->tokv.i, ctx->temp++;
		bs_need(ctx, EQ), bs_expr(ctx), bs_inst2(ctx, STORE_, var);
		bs_need(ctx, TO), bs_expr(ctx);
		*--ctx->csp = ctx->cpc, bs_inst3(ctx, FOR_, var, 0);
		*--ctx->csp = var, *--ctx->csp = ctx->cpc - 1, *--ctx->csp = FOR;
		break;
	case IF: /* CSTK: {IF,N,ENDS...,TEST-FALSE} */
		bs_expr(ctx), bs_inst2(ctx, FALSE_, 0), *--ctx->csp = ctx->cpc - 1;
		if (bs_want(ctx, THEN))
		{
			bs_stmt(ctx);
			ctx->prg[*ctx->csp++] = ctx->cpc;
		}
		else
			ctx->fcompile++, *--ctx->csp = 0, *--ctx->csp = IF;
		break;
	case ELSE:
		n = ctx->csp[1] + 1;
		bs_inst2(ctx, JMP_, 0); /* JUMP OVER "ELSE" */
		*--ctx->csp = IF, ctx->csp[1] = n, ctx->csp[2] = ctx->cpc - 1; /* ADD A FIXUP */
		ctx->prg[ctx->csp[2 + n]] = ctx->cpc; /* PATCH "ELSE" */
		ctx->csp[2 + n] = !bs_want(ctx, IF) ? 0 : /* "ELSE IF" */
			(bs_expr(ctx), bs_inst2(ctx, FALSE_, 0), ctx->cpc - 1);
		break;
	case END:
		bs_need(ctx, *ctx->csp++), ctx->fcompile--; /* MATCH BLOCK */
		if (ctx->csp[-1] == SUB)
		{
			bs_inst2(ctx, RETURN_, *ctx->csp++);
			ctx->prg[*ctx->csp++] = ctx->cpc; /* PATCH JUMP */
		}
		else if (ctx->csp[-1] == WHILE)
		{
			ctx->prg[*ctx->csp++] = (ctx->cpc + 2); /* PATCH TEST */
			bs_inst2(ctx, JMP_, *ctx->csp++); /* LOOP TO TEST */
		}
		else if (ctx->csp[-1] == FOR)
		{
			ctx->prg[*ctx->csp++] = (ctx->cpc + 4); /* PATCH TEST */
			bs_inst2(ctx, NEXT_, *ctx->csp++); /* INCREMENT */
			bs_inst2(ctx, JMP_, *ctx->csp++); /* LOOP TO TEST */
			ctx->temp--; /* ONE LESS TEMP */
		}
		else if (ctx->csp[-1] == IF)
		{
			for (n = *ctx->csp++; n--;) /* PATCH BLOCK ENDS */
				ctx->prg[*ctx->csp++] = ctx->cpc;
			if ((n = *ctx->csp++))
				ctx->prg[n] = ctx->cpc; /* PATCH "ELSE" */
		}
		break;
	case NAME:
		var = ctx->tokv.i;
		if (bs_want(ctx, EQ))
			bs_expr(ctx), bs_inst2(ctx, STORE_, var);
		else if (bs_want(ctx, LP))
			bs_expr(ctx), bs_need(ctx, RP), bs_need(ctx, EQ), bs_expr(ctx), bs_inst2(ctx, STOREI_, var);
		else if (ctx->kwdhook && ctx->kwdhook(ctx, ctx->tokn)) //(!ctx->kwdhook || !ctx->kwdhook(ctx, ctx->tokn))
		{
			ctx->mode[var] = 3;
		}
		else
		{
			int32_t n = 0;
			LIST(bs_expr(ctx); n++);

#if (ENABLE_MATH == ON)
			if (!bs_parse_mathfn(ctx, var, n))
#endif
			if (!ctx->funhook || !ctx->funhook(ctx, ctx->name[var], n))
			{
				if (ctx->mode[var] != 2 || n != SUB(var, 1))
				{
					ctx->mode[var] = 3;
					bs_inst3(ctx, CALL_DYN_, bs_hash(ctx->name[var]), n);
					//bs_bad(ctx, ERR_COUNT);
				}
				else
				{
					bs_inst2(ctx, CALL_, var);
				}
			}
		}
		break;
	case DIM:
		bs_need(ctx, NAME), ctx->mode[var = ctx->tokv.i] = 1; /* SET VAR MODE TO DIM */
		bs_need(ctx, LP), bs_expr(ctx), bs_need(ctx, RP), bs_inst2(ctx, DIM_, var);
		break;
	case BYE:
		bs_inst1(ctx, BYE_);
		break;
	case GT:
		bs_expr(ctx);
		bs_inst1(ctx, ECHO_);
		break;
	default:
		if (ctx->tok)
			bs_bad(ctx, ERR_STA);
	}
	if (!bs_want(ctx, 0))
		bs_bad(ctx, ERR_TOK_STA);
}

static char* bs_gettxtl(char* lbuf, char *txt, int32_t n)
{
	int32_t i;
	for (i = 0; i < n; i++)
	{
		char c = *txt++;
		if (c == 0)
			return lbuf[i] = 0, NULL;
		else if (c == '\n')
			return lbuf[i] = 0, txt;
		else
			lbuf[i] = c;
	}
	return txt;
}

static int32_t append_pcode(TBASIC_CTX *ctx, void* data, int32_t data_sz, void** pcode, int32_t* pcode_sz)
{
	if (*pcode == NULL)
	{
		*pcode = malloc(data_sz);
		if (*pcode == NULL)
			return -EXIT_ERROR;
		memcpy(*pcode, data, data_sz);
		*pcode_sz = data_sz;
	}
	else
	{
		void* newbuf = realloc(*pcode, *pcode_sz + data_sz);
		if (newbuf == NULL)
		{
			free(*pcode);
			return -EXIT_ERROR;
		}
		*pcode = newbuf;
		memcpy((char*)(*pcode) + *pcode_sz, data, data_sz);
		*pcode_sz += data_sz;
	}
	return EXIT_SUCCESS;
}


int32_t bs_export_pcode(TBASIC_CTX *ctx, void**	pcode, int32_t* pcode_sz)
{
	int32_t  i, code_len, cstr_len, var_len, sub_len, sub_deep, stk_len, dmem_len, vhash_len, vname_len;

	code_len = ctx->cpc;
	cstr_len = (int32_t)(ctx->stabp - ctx->stab);
	var_len = ctx->nvar;
	sub_len = (ctx->cursub + 1);
	stk_len = ctx->stk_sz;
	dmem_len = ctx->dmem_sz;
	vhash_len = ctx->nvar;
	vname_len = ctx->vname_sz;

	sub_deep = 0;
	for (i = 0; i < sub_len; i++)
		if (sub_deep < SUB(i, 0))
			sub_deep = SUB(i, 0);
	sub_deep += 2;

	*pcode_sz = 0;
	*pcode = NULL;

	if (append_pcode(ctx, (char*)&bs_fsign, sizeof(bs_fsign), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;	// write signature segment size
	if (append_pcode(ctx, &code_len, sizeof(code_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;	// write p-code segment size
	if (append_pcode(ctx, &cstr_len, sizeof(cstr_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;	// write string segment size
	if (append_pcode(ctx, &var_len, sizeof(var_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;		// write var segment size
	if (append_pcode(ctx, &sub_len, sizeof(sub_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;		// write sub var-dimension
	if (append_pcode(ctx, &sub_deep, sizeof(sub_deep), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;	// write sub loc-dimension
	if (append_pcode(ctx, &stk_len, sizeof(stk_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;		// write stack segment size
	if (append_pcode(ctx, &dmem_len, sizeof(dmem_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;	// write DIM() segment length
	if (append_pcode(ctx, &vhash_len, sizeof(vhash_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;	// write hash segment length
	if (append_pcode(ctx, &vname_len, sizeof(vname_len), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;	// write names segment length

	if (append_pcode(ctx, ctx->prg, code_len * sizeof(int32_t), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;			// write p-code segment
	if (append_pcode(ctx, ctx->var, var_len * sizeof(TValue), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;			// write value segment

	// write sub segment
	for (i = 0; i<sub_len; i++)
		if (append_pcode(ctx, &SUB(i, 0), sub_deep * sizeof(int32_t), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;

	// write hash of var name table
	if (append_pcode(ctx, ctx->vhash, ctx->vhash_sz * sizeof(uint32_t), pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;

	// write string segment
	if (append_pcode(ctx, ctx->stab, cstr_len, pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;

	// write hash of var name table
	if (append_pcode(ctx, ctx->vname, ctx->vname_sz, pcode, pcode_sz) != EXIT_SUCCESS) goto _EXIT_ERROR;

	return EXIT_SUCCESS;

_EXIT_ERROR:
	free(*pcode);
	return -EXIT_ERROR;
}

static int32_t append_str(char* str_in, char** str_out, uint32_t* obuf_sz)
{
	uint32_t data_sz = strlen(str_in) + 1;
	if (*str_out == NULL)
	{
		*str_out = malloc(data_sz);
		if (*str_out == NULL)
			return -EXIT_ERROR;
		memcpy(*str_out, str_in, data_sz);
		*obuf_sz = data_sz;
	}
	else
	{
		void* newbuf = realloc(*str_out, *obuf_sz + data_sz);
		if (newbuf == NULL)
		{
			free(*str_out);
			return -EXIT_ERROR;
		}
		*str_out = newbuf;
		memcpy((char*)(*str_out) + *obuf_sz, str_in, data_sz);
		*obuf_sz += data_sz;
	}
	return EXIT_SUCCESS;
}


int32_t bs_compile(TBASIC_CTX *ctx, char *bsrc)
{
	char lbuf[MAX_STR_LEN] = { 0 };
	char* psrc = bsrc;
	int32_t i;

	int32_t code = setjmp(ctx->trap); /* RETURN ON ERROR */
	if (code == EXIT_ERROR_COMPILE)
		return -EXIT_ERROR_COMPILE; /* FILE SYNTAX ERROR */

	// CODE GENERATION LOOP
	do
	{
		psrc = bs_gettxtl(lbuf, psrc, sizeof(lbuf));
		ctx->lnum++;
		if (*lbuf != 0)
		{
			ctx->lp = lbuf;
			ctx->ungot = 0;

			bs_stmt(ctx); /* PARSE AND COMPILE */

			if (!ctx->fcompile)
				break;
		}
	} while (psrc);

	ctx->ipc = ctx->cpc + 1;
	ctx->fcompile = 0;
	bs_inst1(ctx, BYE_);

	// Create export name table
	for (i = 0; i < ctx->nvar; i++)
	{
		if ((ctx->mode[i] == 0) || (ctx->mode[i] == 1))
		{
			append_str(ctx->name[i], &ctx->vname, &ctx->vname_sz);
		}
	}
	append_str("", &ctx->vname, &ctx->vname_sz);	// end of table marker

	/* TEST CODE:
	char* nptr = ctx->vname;
	while (*nptr)
	{
		printf("%s\n", nptr);
		nptr += strlen(nptr) + 1;
	}
	free(ctx->vname);
	*/

	/* (RE) CALCULATE VAR'S HASH TABLE */
	ctx->vhash_sz = ctx->nvar;
	ctx->vhash = calloc(1, ctx->vhash_sz * sizeof(uint32_t));
	if (ctx->vhash)
		for (i = 0; i < ctx->nvar; i++)
		{
			// calculate hash for variables only
			if ((ctx->mode[i] == 0) || (ctx->mode[i] == 1))
				ctx->vhash[i] = bs_hash(ctx->name[i]);
			else
				ctx->vhash[i] = 0;
		}

	return EXIT_SUCCESS;
}

int32_t bs_listing(TBASIC_CTX *ctx, char *bsrc, int32_t level, FILE* lst)
{
	char asml[MAX_STR_LEN];
	char lbuf[MAX_STR_LEN] = { 0 };
	int32_t lnum = 0;
	int32_t addr = 0;
	char* psrc = bsrc;
	int32_t ix;

	if (lst == NULL)
		lst = stdout;

	if (level & LST_PRINT_LISTING)
	{
		int32_t nerr;
		int32_t nline;
		char* msg = NULL;
		nerr = bs_last_error(ctx, &nline, NULL, &msg);

#if (ENABLE_DASM == ON)
		// print source code line and disassembled p-code
		do
		{

			psrc = bs_gettxtl(lbuf, psrc, sizeof(lbuf));
			lnum++;

			if (*lbuf == 0)
				continue;

			fprintf(lst, "\n%04d:\t%s\n", lnum, lbuf);

			if (nerr != EXIT_SUCCESS && nline == lnum)
			{
				fprintf(lst, "\t! COMPILE ERROR IN LINE %d: %s\n", nline, msg);
				break;
			}

			while (GET_LINEn(ctx->prg[addr]) == lnum)
			{
				addr += bs_dasm(ctx, addr, asml);
				fprintf(lst, "%s\n", asml);
			}
		} while (psrc);


		fprintf(lst, "\nVAR/SUB NAME TABLE\n---------------------\n");
		for (ix = 0; ix < ctx->nvar; ix++)
		{
			//char sval[FMTSZ];
			//bs_val2str(ctx, sval, ctx->var[ix].v);
			//fprintf(lst, "0x%08X\t0x%08X(%16s)\t%s\n", ix, ctx->var[ix].i, sval, ctx->name[ix]);
			char* smode[4] = {"VAR", "ARR", "SUB", "FUN"};
			fprintf(lst, "0x%08X\t0x%08X\t%s\t%s\n", ix, ctx->var[ix].i, smode[ctx->mode[ix]], ctx->name[ix]);
		}
#endif
	}

	if (level & LST_PRINT_REPORT)
	{
		int32_t code_len, cstr_len, var_len, sub_len, sub_deep;
		code_len = ctx->cpc * sizeof(TData);
		cstr_len = (int32_t)(ctx->stabp - ctx->stab);
		var_len = (ctx->nvar) * sizeof(TValue);
		sub_len = ctx->cursub + 1;
		sub_deep = 0;
		for (ix = 0; ix < sub_len; ix++)
			if (sub_deep < SUB(ix, 0))
				sub_deep = SUB(ix, 0);
		sub_deep += 2;

		fprintf(lst, "\n\nCOMPILE REPORT\n");
		fprintf(lst, "---------------\n");
		fprintf(lst, "TOTAL VARIABLES :\t%d\n", ctx->nvar);
		fprintf(lst, "CODE SEGMENT SIZE:\t%d\n", code_len);
		fprintf(lst, "VAR SEGMENT SIZE:\t%d\n", var_len);
		fprintf(lst, "SUB SEGMENT SIZE:\t%d (VARS=%d; LOCS=%d)\n", (int)(sub_len*sub_deep * sizeof(int32_t)), sub_len, sub_deep-2);
		fprintf(lst, "STRING SEGMENT SIZE:\t%d\n", cstr_len);
		fprintf(lst, "CONTEXT SIZE:\t\t%d\n", (int)sizeof(TBASIC_CTX));
		fprintf(lst, "\n");
	}

	return EXIT_SUCCESS;
}

#endif

int32_t bs_reset(TBASIC_CTX *ctx)
{
	ctx->pc = 0;
	ctx->sp = ctx->stk + ctx->stk_sz;
	return EXIT_SUCCESS;
}

int32_t bs_run(TBASIC_CTX *ctx, TTraceHook thook)
{
	int32_t code = setjmp(ctx->trap); /* RETURN ON ERROR */
	if (code == EXIT_ERROR_RUNTIME)
		return -EXIT_ERROR_RUNTIME;
	if (code == EXIT_BYE)
		return EXIT_SUCCESS; /* "BYE" */

	/* RUN PROGRAM */
	bs_reset(ctx);

	while (1)
	{
		int32_t opcode = ctx->prg[ctx->pc];

		if (thook)
			thook(ctx, ctx->pc, GET_OPCODE(opcode), GET_LINEn(opcode));

		ctx->pc++;

		if (!optable[GET_OPCODE(opcode)](ctx))
			break;

#if (ENABLE_CHECK_STACK == ON)
		if (ctx->sp <= (ctx->stk + 2))
			ctx->pc--, bs_err(ctx, ERR_STACK_OVERFLOW);

#endif
	}
	return -EXIT_ERROR_RUNTIME;
}


int32_t bs_get_var(TBASIC_CTX *ctx, char* name, TValue* val)
{
	char* spr;
	int32_t i;
	uint32_t hash;
	char vname[SYMSZ];
	int32_t idx = -1;	// (idx == -1) is no array; (idx == 0) is array length; (idx => 0) is array element;

	if (name)
	{
		strncpy(vname, name, SYMSZ);

		// check that variable is array and get index
		spr = strchr(vname, '(');
		if (spr)
		{
			idx = atoi(spr + 1);
			*spr = 0;	// keep var name
		}

		// search for variable in hash table
		hash = bs_hash(vname);
		for (i = 0; i < ctx->vhash_sz; i++)
		{
			if (hash == ctx->vhash[i])
			{
				if (idx == -1)
					*val = ctx->var[i];	// return regular variable
				else if (ctx->var[i].p == NULL)	// check that array is defined
					break;
				if (idx == 0)	// index 0 is array length type int32_t; convert it to DATATYPE
#if (DATATYPE == TYPE_FX16Q16)
					val->v = fix16_from_int(((TValue*)ctx->var[i].p + idx)->i);
#elif (DATATYPE == TYPE_FLOAT32)
					val->v = roundf((float)((TValue*)ctx->var[i].p + idx)->i);
#else
					*val = *((TValue*)ctx->var[i].p + idx);
#endif
				else if (idx > 0)	//	return array element
						*val = *((TValue*)ctx->var[i].p + idx);
				return EXIT_SUCCESS;
			}
		}
	}
	return -EXIT_ERROR;
}

int32_t bs_set_var(TBASIC_CTX *ctx, char* name, TValue val)
{
	char* spr;
	int32_t i;
	uint32_t hash;
	char vname[SYMSZ];
	int32_t idx = -1;	// (idx == -1) is no array; (idx == 0) is array length; (idx => 0) is array element;

	if (name)
	{
		strncpy(vname, name, SYMSZ);

		// check that variable is array and get index
		spr = strchr(vname, '(');
		if (spr)
		{
			idx = atoi(spr + 1);
			*spr = 0;	// keep var name
		}

		// search for variable in hash table
		hash = bs_hash(vname);
		for (i = 0; i < ctx->vhash_sz; i++)
		{
			if (hash == ctx->vhash[i])
			{
				if (idx == -1)
					ctx->var[i]  = val;	// set regular variable
				else if (ctx->var[i].p == NULL)	// check that array is defined
					break;
				if (idx == 0)	// index 0 is array length and can't be changed
					return -EXIT_ERROR;
				else if (idx > 0)	//	set array element
					*((TValue*)ctx->var[i].p + idx) = val;
				return EXIT_SUCCESS;
			}
		}
	}
	return -EXIT_ERROR;
}


int32_t  bs_val2str(TBASIC_CTX *ctx, char* str, TData val)
{
	if (!str)
		return -EXIT_ERROR;

#if (DATATYPE == TYPE_FLOAT32)
	bs_sprintf(ctx, str, "%g", val);
#elif (DATATYPE == TYPE_INT32)
	bs_sprintf(ctx, str, "%d", val);
#elif (DATATYPE == TYPE_FX16Q16)
	bs_fx16tostr(val, str, 12);
#endif
	return EXIT_SUCCESS;
}

int32_t  bs_reg_dyn_fn(TBASIC_CTX *ctx, TCode code, char* name)
{
	uint32_t idx = ctx->dynf_sz;
	ctx->dynf_list = realloc(ctx->dynf_list, (ctx->dynf_sz + 1) * sizeof(struct TDynDesc));
	if (!ctx->dynf_list)
		return -EXIT_ERROR;
	ctx->dynf_sz++;

	ctx->dynf_list[idx].name_hash = bs_hash(name);
	ctx->dynf_list[idx].fn = code;

	return EXIT_SUCCESS;
}
