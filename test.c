#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "basic.h"
#include "math.h"

int32_t PRINTS_(TBASIC_CTX *ctx) 
{ 
	char* s = &ctx->stab[(*ctx->sp++).i];
	puts(s); 
	return 1;
}

int32_t EXPORT_(TBASIC_CTX *ctx)
{
	char str2[16];
	char str3[16];

	char* arg1 = "";
	TValue arg2;
	TValue arg3;

	uint32_t narg = ctx->prg[(ctx->pc)++];

	switch (narg)
	{
	case 0:
		printf("EXPORT\n");
		break;
	case 1:
		arg1 = &ctx->stab[(*ctx->sp++).i];
		printf("EXPORT %s\n", arg1);
		break;
	case 2:
		arg2 = (*ctx->sp++);
		arg1 = &ctx->stab[(*ctx->sp++).i];
		bs_val2str(ctx, str2, arg2.v);
		printf("EXPORT %s, %s\n", arg1, str2);
		break;
	case 3:
		arg3 = (*ctx->sp++);
		arg2 = (*ctx->sp++);
		arg1 = &ctx->stab[(*ctx->sp++).i];
		bs_val2str(ctx, str3, arg3.v);
		bs_val2str(ctx, str2, arg2.v);
		printf("EXPORT %s, %s, %s\n", arg1, str2, str3);
		break;
	default:
		break;
	}


//	bs_val2str(ctx, str3, arg3.v);
//	bs_val2str(ctx, str2, arg2.v);

//	printf("EXPORT %s, %s, %s\n", arg1, str2, str3);
	return 1;
}



int32_t NEWFUN_(TBASIC_CTX *ctx)
{
	TData v;
	v = (*ctx->sp).v;					

#if (DATATYPE == TYPE_FLOAT32)
	v = v + 1.0;
#elif (DATATYPE == TYPE_INT32)
	v = v + 1;
#elif (DATATYPE == TYPE_FX16Q16)
	v = v + F16(1);
#endif

	(ctx->sp)->v = v;
	return 1;
}

int32_t DYN_FUN_1_(TBASIC_CTX *ctx)
{
	int nparam = DYF_NARG();
	TValue arg2 = DYF_ARG(2);
	char* arg1 = &ctx->stab[DYF_ARG(1).i];
	//ctx->sp += 2;

	TValue val;

	if (bs_get_var(ctx, arg1, &val) == EXIT_SUCCESS)
	{
		char sval[16] = { 0 };
		bs_val2str(ctx, sval, val.v);
		printf("%s=%s\n", arg1, sval);
	}

	val.v = arg2.v;

	if (bs_set_var(ctx, arg1, val) == EXIT_SUCCESS)
	{
		if (bs_get_var(ctx, arg1, &val) == EXIT_SUCCESS)
		{
			char sval[16] = { 0 };
			bs_val2str(ctx, sval, val.v);
			printf("%s=%s\n", arg1, sval);
		}
	}

	DYF_RETVAL(val.v);

	return 1;
}

int32_t DYN_FUN_2_(TBASIC_CTX *ctx)
{
	int n = DYF_NARG();
	TValue arg1 = DYF_ARG(1);
	TValue arg2 = DYF_ARG(2);

	if (n==2)
		DYF_RETVAL(arg1.v - arg2.v);		// do always, even whan no value returned
	else
		DYF_RETVAL(0);
	return 1;
}


#if (ENABLE_COMPILER == ON)

int32_t kwdhook_(TBASIC_CTX *ctx, char *msg) 
{
	if (!strcmp(msg, "PRINTS"))
	{
		bs_expr(ctx), 
		bs_inst1(ctx, PRINTS_);
	}
	else if (!strcmp(msg, "EXPORT"))
	{
		int32_t n = bs_get_nparam(ctx);
		bs_inst2(ctx, EXPORT_, n);
	}
	else
	{
		return 0;
	}
	return 1;
}

int32_t fnhook_(TBASIC_CTX *ctx, char *name, int n)
{
	if (!strcmp(name, "NEWFUN") && n == 1)
	{
		bs_inst1(ctx, NEWFUN_);
	}
	else 
	{
		return 0;
	}

	return 1;
}
#endif

int32_t trace(TBASIC_CTX *ctx, int addr, int opcode, int line)
{
	/*
	char sval[16] = { 0 };
	TValue val;
	if (bs_get_var(ctx, "CC(0)", &val) == EXIT_SUCCESS)
	{
		bs_val2str(ctx, sval, val.v);
		printf("CC(0)=%s\n", sval);
	}
	*/

#if (ENABLE_DASM == ON)
	char asml[255];
	bs_dasm(ctx, addr, asml);
	printf("%s\t\t; LINE: %d\n", asml, line);
#else
	printf("LINE: %d\n", line);
#endif

	return 0;
}

void run(TBASIC_CTX *ctx, TTraceHook thook)
{
	if (bs_run(ctx, thook) != EXIT_SUCCESS)
	{
		int nerr;
		int nline;
		int addr;
		char* msg = NULL;

		nerr = bs_last_error(ctx, &nline, &addr, &msg);
		if (nerr != EXIT_SUCCESS)
		{
			printf("RUN-TIME ERROR AT ADDR: 0x%08X (LINE %d) %s\n", addr, nline, msg);
		}
	}
}

void display_usage()
{
#if (ENABLE_COMPILER == ON)
	printf("Usage:\n");
	printf("basic [-o pcode_file][-l list_file][-r pcode_file][-v][-t] src_file\n");
	printf(" -o pcode_file  Output p-code file name.\n");
	printf(" -l list_file   Listing output file name.\n");
	printf(" -r pcode_file  Run p-code file.\n");
	printf(" -v             Verbose listing\n");
	printf(" -t             Enable run-time trace\n");
	printf("Example:\n");
	printf(" basic -o test.pcode -l test.lst -v test.bas\n");
	printf(" basic -t -r test.pcode\n");
#else
	printf("Usage:\n");
	printf("basic -r pcode_file\n");
	printf(" -r pcode_file  Run p-code file.\n");
	printf(" -t             Enable run-time trace\n");
	printf("Example:\n");
	printf(" basic -t -r test.pcode\n");
#endif
}

int     opterr = 1,             /* if error message should be printed */
optind = 1,             /* index into parent argv vector */
optopt,                 /* character checked for validity */
optreset;               /* reset getopt */
char    *optarg;                /* argument associated with option */

#define BADCH   (int)'?'
#define BADARG  (int)':'
#define EMSG    ""

/*
* getopt --
*      Parse argc/argv argument vector.
*/
int optget(int nargc, char * const nargv[], const char *ostr)
{
	static char *place = EMSG;              /* option letter processing */
	const char *oli;                        /* option letter list index */

	if (optreset || !*place) {              /* update scanning pointer */
		optreset = 0;
		if (optind >= nargc || *(place = nargv[optind]) != '-') {
			place = EMSG;
			return (-1);
		}
		if (place[1] && *++place == '-') {      /* found "--" */
			++optind;
			place = EMSG;
			return (-1);
		}
	}                                       /* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' ||
		!(oli = strchr(ostr, optopt))) {
		/*
		* if the user didn't specify '-' as an option,
		* assume it means -1.
		*/
		if (optopt == (int)'-')
			return (-1);
		if (!*place)
			++optind;
		if (opterr && *ostr != ':')
			(void)printf("illegal option -- %c\n", optopt);
		return (BADCH);
	}
	if (*++oli != ':') {                    /* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	}
	else {                                  /* need an argument */
		if (*place)                     /* no white space */
			optarg = place;
		else if (nargc <= ++optind) {   /* no arg */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (opterr)
				(void)printf("option requires an argument -- %c\n", optopt);
			return (BADCH);
		}
		else                            /* white space */
			optarg = nargv[optind];
		place = EMSG;
		++optind;
	}
	return (optopt);                        /* dump back option letter */
}

int main(int argc, char **argv)
{
	TBASIC_CTX ctx;
	char* pcode_name = NULL;
	char* src_name = NULL;
	char* lst_name = NULL;

	char* opts = "l:o:r:vth?";
	int opt = 0;
	int verbosity = LST_PRINT_REPORT;
	int need_compile = 0;
	int need_run = 0;
	int need_trace = 0;

	opt = optget(argc, argv, opts);
	while (opt != -1) 
	{
		switch (opt) 
		{
		case 'l':
			lst_name = optarg;
			break;

		case 'o':
			need_compile = 1;
			pcode_name = optarg;
			break;

		case 'v':
			verbosity = LST_PRINT_ALL;
			break;

		case 'r':
			need_run = 1;
			pcode_name = optarg;
			break;

		case 't':
			need_trace = 1;
			break;

		case 'h':   /* fall-through is intentional */
		case '?':
			display_usage();
			break;

		default:
			/* You won't actually get here. */
			break;
		}
		opt = optget(argc, argv, opts);
	}

/*
	for (int ix = optind; ix < argc; ix++)
		printf("Non-option argument %s\n", argv[ix]);
*/
	src_name = argv[optind];

	if (!need_compile && !need_run)
	{
		display_usage();
		return -1;
	}

#if (ENABLE_COMPILER == ON)

	if (need_compile)
	{
		FILE *sf;

		if (!src_name)
		{
			printf("Parameter error: no source file specified.\n");
			return 255;
		}

		bs_init_alloc(&ctx, vsprintf, putchar, NULL, NULL);

		if (sf = fopen(src_name, "rb"))
		{
			fseek(sf, 0, SEEK_END);
			long fsize = ftell(sf);
			fseek(sf, 0, SEEK_SET);  //same as rewind(f);

			char *psrc = malloc(fsize + 1);
			if (psrc)
			{
				fread(psrc, fsize, 1, sf);
				fclose(sf);
				psrc[fsize] = 0;

				bs_reg_keyword(&ctx, kwdhook_);
				bs_reg_func(&ctx, fnhook_);
				bs_reg_opcode(&ctx, NEWFUN_, "NEWFUN");
				bs_reg_opcode(&ctx, EXPORT_, "EXPORT");
				bs_reg_opcode(&ctx, PRINTS_, "PRINTS");

				if (bs_compile(&ctx, psrc) == EXIT_SUCCESS)
				{
					//run(&ctx, need_trace ? (trace) : (NULL));
					FILE *sf;
					if ((sf = fopen(pcode_name, "wb")) != NULL)
					{
						void*	pcode; /* P-CODE OUTPUT BUFFER */
						int		pcode_sz; /* P-CODE BUFFER SIZE */
						if (bs_export_pcode(&ctx, &pcode, &pcode_sz) == EXIT_SUCCESS)
						{
							if (fwrite(pcode, pcode_sz, 1, sf) != 1)
								printf("ERROR WRITE FILE '%s'\n", pcode_name);
						}
						else
						{
							printf("ERROR EXPORT P-CODE.\n");
						}
						fclose(sf);
					}
				}
				else
				{
					int nerr, nline;
					char* msg = NULL;
					nerr = bs_last_error(&ctx, &nline, NULL, &msg);
					if (nerr != EXIT_SUCCESS)
						printf("COMPILE ERROR IN LINE %d: %s\n", nline, msg);
				}

				if (lst_name)
				{
					FILE* flst = fopen(lst_name, "w+");
					bs_listing(&ctx, psrc, verbosity, flst);
					if (flst)
						fclose(flst);
				}
			}

			free(psrc);
			bs_free(&ctx);
		}
		else
		{
			printf("Couldn't open file %s\n", src_name);
			return 255;
		}
	}
#endif

	if (need_run)
	{
		FILE *sf;

		if (!pcode_name)
		{
			printf("Parameter error: no p-code file specified.\n");
			return 255;
		}

		if ((sf = fopen(pcode_name, "rb")) != NULL)
		{
			fseek(sf, 0, SEEK_END);
			long fsize = ftell(sf);
			fseek(sf, 0, SEEK_SET);  //same as rewind(f);

			char* pcode = (char*)malloc(fsize);
			if (!pcode || (fread(pcode, fsize, 1, sf) != 1))
			{
				printf("ERROR READ FILE '%s'\n", pcode_name);
				free(pcode);
				return -1;
			}
			fclose(sf);

			int err = bs_init_load(&ctx, pcode, vsprintf, putchar, NULL, NULL);

			if (err == EXIT_SUCCESS)
			{
				bs_reg_opcode(&ctx, NEWFUN_, "NEWFUN");
				bs_reg_opcode(&ctx, EXPORT_, "EXPORT");
				bs_reg_opcode(&ctx, PRINTS_, "PRINTS");
				bs_reg_dyn_fn(&ctx, DYN_FUN_1_, "DYN_FUN_1");
				bs_reg_dyn_fn(&ctx, DYN_FUN_2_, "DYN_FUN_2");

				{
					TValue v;
					v.v = 10;	bs_set_var(&ctx, "SNR_THR", v);
					v.v = 20;	bs_set_var(&ctx, "EMI_THR", v);
				}

				if (bs_run(&ctx, need_trace ? (trace) : (NULL)) != EXIT_SUCCESS)
				{
					int nerr, nline;
					char* msg = NULL;
					nerr = bs_last_error(&ctx, &nline, NULL, &msg);
					if (nerr != EXIT_SUCCESS)
						printf("RUN-TIME ERROR IN LINE %d: %s\n", nline, msg);
				}
			}
			else if (err == -EXIT_ERROR_VERSION)
			{
				printf("ERROR EXECUTE FILE '%s';  VESRION MISMATCH\n", pcode_name);
			}
			else if (err == -EXIT_ERROR_ALLOC)
			{
				printf("ERROR EXECUTE FILE '%s';  MEMORY ALLOCATION\n", pcode_name);
			}

			free(pcode);
			bs_free(&ctx);
		}
	}

	return 0;
}
