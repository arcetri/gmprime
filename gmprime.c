/*
 * gmprime - gmp h*2^n-1 Riesel primality tester
 *
 * usage:
 *
 *      gmprime h n
 *
 *      h       power of 2 multuplier (as in h*2^n-1)
 *      n       power of 2 (as in h*2^n-1)
 *
 * NOTE: Sometimes u(2) is called u(0).
 *
 * NOTE: This is for demo purposes only.  One should certailny improve
 *       the robustness and performance of this code!!!
 *
 *       For more information on calc, see:
 *
 *           http://www.isthe.com/chongo/tech/comp/calc/index.html
 *
 *       The source to calc is freely available via links from the above URL.
 *
 * Exit codes:
 *
 *      0       h*2^n-1 is prime (also prints "prime" to stdout)
 *      1       h*2^n-1 is not prime (also prints "composite" to stdout)
 *      >1      some error occurred
 *
 * NOTE: Comments in this source use 2^n to mean 2 raised to the power of n, not xor.
 *
 * Copyright (c) 2013,2017,2018 by Landon Curt Noll.  All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright, this permission notice and text
 * this comment, and the disclaimer below appear in all of the following:
 *
 *       supporting documentation
 *       source copies
 *       source works derived from this source
 *       binaries derived from this source or from derived source
 *
 * LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
 * EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * chongo (Landon Curt Noll, http://www.isthe.com/chongo/index.html) /\oo/\
 *
 * Share and enjoy! :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <gmp.h>
#include <ctype.h>
#include <getopt.h>

#include "lucas.h"
#include "debug.h"
#include "checkpoint.h"

/*
 * constants
 */
#define MAX_H_N_LEN BUFSIZ	/* more than enougn for h and n that we care about */

/*
 * globals
 */
const char *program = NULL;	/* our name */
const char version_string[] = "demo/gmprime-2.0";	/* package name and version */
int debuglevel = DBG_NONE;	/* if > 0 then be verbose */
static const char *usage = "[-v level] [-c] [-t] [-T] [-d checkpoint_dir] [-s secs] [-h] h n\n"
    "\n"
    "\t-v level\tverbosity level, debug msgs go to stderr (def: output only the test result to stdout)\n"
    "\t-c\t\toutput to stdout, calc code that may be used to verify partial results\n"
    "\t\t\t    example: gmprime -c 15 31 | calc -p\n"
    "\t-t\t\toutput total prime test times to stderr, (def: do not)\n"
    "\t-T\t\toutput extended prime test times to stderr, (def: do not)\n"
    "\t\t\t    NOTE: -T implies -t\n"
    "\t-d checkpoint_dir\tform checkpoint files under checkpoint_dir (def: do not checkpoint)\n"
    "\t-s secs\t\tcheckpoint about every secs seconds (def: 3600 seconds)\n"
    "\t\t\t    NOTE: -s secs requires -d checkpoint_dir\n"
    "\t\t\t    NOTE: secs must be >= 0, secs == 0 ==> checkpoint every term\n"
    "\n"
    "\t-h\t\tprint this help message and exit 2\n"
    "\n"
    "\th\t\tpower of 2 multuplier (as in h*2^n-1) must be > 0 and < 2^n\n"
    "\tn\t\tpower of 2 (as in h*2^n-1) must be > 0\n"
    "\n"
    "\tExit codes:\n"
    "\n"
    "\t0\th*2^n-1 is prime (also prints 'prime' to stdout)\n"
    "\t1\th*2^n-1 is not prime (also prints 'composite' to stdout)\n" "\t>1\tsome error occurred\n";

/*
 * list of very small verified Riesel primes that we special case
 */
struct h_n {
    unsigned long h;		/* multiplier of 2 */
    unsigned long n;		/* power of 2 */
};
static const struct h_n small_h_n[] = {
    {1, 2},			/* 1 * 2 ^ 2 - 1 = 3 is prime */

    {0, 0}			/* MUST BE THE LAST ENTRY! */
};
static const struct h_n composite_h_n[] = {
    {1, 1},			/* 1 * 2 ^ 1 - 1 = 1 is not prime */

    {0, 0}			/* MUST BE THE LAST ENTRY! */
};


/*
 * test h*2^n-1 for primality
 */
int
main(int argc, char *argv[])
{
    char *h_arg;		/* h as a string */
    char *n_arg;		/* n as a string */
    mpz_t pow_2;		/* 2^n */
    mpz_t h_pow_2;		/* h*(2^n) */
    mpz_t riesel_cand;		/* Riesel candidate to test - n*(2^n)-1 */
    mpz_t u_term;		/* Riesel sequence value */
    mpz_t u_term_sq;		/* square of prev term */
    mpz_t u_term_sq_2;		/* square - 2 of prev term */
    mpz_t J;			/* used in mod calculation - u_term_sq_2 / (2^n) */
    mpz_t K;			/* used in mod calculation - u_term_sq_2 mod (2^n) */
    mpz_t J_div_h;		/* used in mod calculation - int(J/h) */
    mpz_t J_mod_h;		/* used in mod calculation - J mod h then (J mod h)*(2^n) */
    int c;			/* option */
    unsigned long i;		/* u term index - 1st Lucas term is U(2) = v(h) - 1st index is 2 */
    // XXX unsigned long restored_i = 0;	/* u term index restored from checkpoint or 0 ==> test not started */
    /*
     * For Mersenne numbers, U(2) == 4
     */
    unsigned long h;		/* multiplier of 2 */
    unsigned long n;		/* power of 2 */
    unsigned long orig_h;	/* original value of h */
    unsigned long orig_n;	/* original value of n */
    unsigned long v1;		/* v(1) for h and n */
    char h_str[MAX_H_N_LEN + 1];	/* h as a string */
    char n_str[MAX_H_N_LEN + 1];	/* h as a string */
    int h_len;			/* length of string in h_str */
    int n_len;			/* length of string in n_str */
    const struct h_n *h_n_p;	/* pointer into small_h_n */
    int calc_mode = 0;		/* output calc code so calc can verify partial results */
    int write_stats = 0;	/* output total prime stats to stderr */
    int write_extended_stats = 0;	/* output extended prime stats to stderr */
    char *checkpoint_dir = NULL;	/* form checkpoint files under checkpoint_dir */
    int checkpoint_secs = DEF_CHKPT_SECS;	/* checkpoint every checkpoint_secs seconds */
    int have_s = 0;		/* if we saw an -s secs argument */
    extern int optind;		/* argv index of the next arg */
    extern char *optarg;	/* optional argument */

    /*
     * parse args
     */
    program = argv[0];
    while ((c = getopt(argc, argv, "v:ctTd:s:h")) != -1) {
	switch (c) {
	case 'v':
	    debuglevel = strtol(optarg, NULL, 0);
	    break;
	case 'c':
	    calc_mode = 1;
	    break;
	case 't':
	    write_stats = 1;
	    break;
	case 'T':
	    write_stats = 1;
	    write_extended_stats = 1;
	    break;
	case 'd':
	    checkpoint_dir = optarg;
	    break;
	case 's':
	    checkpoint_secs = strtol(optarg, NULL, 0);
	    have_s = 1;
	    break;
	case 'h':
	    fprintf(stderr, "usage: %s %s", program, usage);
	    exit(2);
	    break;
	default:
	    fprintf(stderr, "usage: %s %s", program, usage);
	    exit(3);
	}
    }
    argv += (optind - 1);
    argc -= (optind - 1);
    if (argc != 3) {
	usage_err(4, __func__, "expected only 3 args");
	exit(4); // NOT REACHED
    }
    if (have_s && checkpoint_dir == NULL) {
	usage_err(5, __func__, "use of -s secs requires -d checkpoint_dir\n");
	exit(5); // NOT REACHED
    }
    if (checkpoint_secs < 0) {
	usage_err(6, __func__, "secs: %d must be >= 0\n", checkpoint_secs);
	exit(6); // NOT REACHED
    }
    h_arg = argv[1];
    errno = 0;
    h = strtoul(h_arg, NULL, 0);
    if (strchr(h_arg, '-') != NULL || errno != 0 || h <= 0) {
	usage_err(7, __func__, "FATAL: h must an integer > 0\n");
	exit(7); // NOT REACHED
    }
    n_arg = argv[2];
    errno = 0;
    n = strtoul(n_arg, NULL, 0);
    if (strchr(n_arg, '-') != NULL || errno != 0 || n <= 0) {
	usage_err(8, __func__, "FATAL: n must an integer > 0\n");
	exit(8); // NOT REACHED
    }

    /*
     * convert even h into odd h by increasing n
     */
    /*
     * save our argument values for debugging and final reporting
     */
    orig_h = h;
    orig_n = n;
    /*
     * force h to become odd
     */
    if (h % 2 == 0) {
	dbg(DBG_MED, "converting even h: %ld into odd by increasing n: %ld\n", orig_h, orig_n);
	while (h % 2 == 0 && h > 0) {
	    h >>= 1;
	    ++n;
	}
	dbg(DBG_LOW, "new equivalent h: %lu and new equivalent n: %ld\n", h, n);
	if (h <= 0) {
	    err(9, __func__, "new equivalent h: %lu <= 0\n", h);
	    exit(9); // NOT REACHED
	}
    }
    /*
     * form string based on possibly modified h
     */
    h_str[0] = '\0';		// paranoia
    h_str[MAX_H_N_LEN] = '\0';	// paranoia
    errno = 0;
    h_len = snprintf(h_str, MAX_H_N_LEN, "%lu", h);
    if (h_len < 0 || h_len >= MAX_H_N_LEN) {
	errp(10, __func__, "converting h: %lu to string via snprintf returned: %d\n", h, h_len);
	exit(10); // NOT REACHED
    }
    h_str[h_len] = '\0';	// paranoia
    /*
     * form string based on possibly modified n
     */
    n_str[0] = '\0';		// paranoia
    n_str[MAX_H_N_LEN] = '\0';	// paranoia
    errno = 0;
    n_len = snprintf(n_str, MAX_H_N_LEN, "%lu", n);
    if (n_len < 0 || n_len >= MAX_H_N_LEN) {
	errp(11, __func__, "converting n: %lu to string via snprintf returned: %d\n", n, n_len);
	exit(11); // NOT REACHED
    }
    n_str[n_len] = '\0';	// paranoia

    /*
     * firewall - catch the special cases for small primes
     *
     * NOTE: This case normally fails the standard Riesel test because n is too small.
     */
    for (h_n_p = small_h_n; h_n_p->h > 0 && h_n_p->n > 0; ++h_n_p) {
	if (h == h_n_p->h && n == h_n_p->n) {
	    if (calc_mode) {
		printf("read lucas;\n");
		printf("print \"lucas( %ld , %lu )\",;", h, n);
		printf("ret = lucas(%ld , %ld);\n", h, n);
		printf("if (ret == 1) { print \"returned prime\"; } else { print \"failed returning\", ret; };\n");
		printf("print \"%s: origianl test: %ld * 2 ^ %ld - 1 =\", (%ld * 2 ^ %ld - 1);\n",
		       program, orig_h, orig_n, orig_h, orig_n);
		printf("print \"%s: %lu * 2 ^ %lu - 1 =\", (%lu * 2 ^ %lu - 1), \"is prime\";\n", program, h, n, h, n);
	    } else {
		printf("%ld * 2 ^ %ld - 1 is prime\n", orig_h, orig_n);
	    }
	    exit(0);
	}
    }

    /*
     * firewall - catch the special cases for small composites
     *
     * NOTE: This case normally fails the standard Riesel test because n is too small.
     */
    for (h_n_p = composite_h_n; h_n_p->h > 0 && h_n_p->n > 0; ++h_n_p) {
	if (h == h_n_p->h && n == h_n_p->n) {
	    if (calc_mode) {
		printf("read lucas;\n");
		printf("print \"lucas( %ld , %lu )\",;", h, n);
		printf("ret = lucas(%ld , %ld);\n", h, n);
		printf("if (ret == 0) { print \"returned composite\"; } else { print \"failed returning\", ret; };\n");
		printf("print \"%s: origianl test: %ld * 2 ^ %ld - 1 =\", (%ld * 2 ^ %ld - 1);\n",
		       program, orig_h, orig_n, orig_h, orig_n);
		printf("print \"%s: %ld * 2 ^ %ld - 1 is composite\";\n", program, orig_h, orig_n);
	    } else {
		printf("%ld * 2 ^ %ld - 1 is composite\n", orig_h, orig_n);
	    }
	    exit(1);
	}
    }

    /*
     * firewall - h*2^n-1 is not a multiple of 3
     *
     * We can check this quickly by looking at h and n.
     * The value h*2^n-1 is multiple of 3 when:
     *
     *          h = 1 mod 3 AND n is even
     * or when:
     *          h = 2 mod 3 AND n is odd
     *
     * If either of those cases is true, don't test for
     * primality because the value is a multiple of 3.
     * We also know that h*2^n-1 is not 3 because the
     * 'catch the special cases for small primes' code
     * would have exited above if h*2^n-1 == 3.
     */
    if (((h % 3 == 1) && (n % 2 == 0)) || ((h % 3 == 2) && (n % 2 == 1))) {
	if (calc_mode) {
	    printf("print \"%s: %ld * 2 ^ %ld - 1 is a multiple of 3 > 3\";\n", program, orig_h, orig_n);
	    printf("mod3 = ((%ld * 2 ^ %ld - 1) %% 3);\n", orig_h, orig_n);
	    printf("if (mod3 == 0) { print \"value mod 3:\", mod3; } else { print \"failed: mod 3 != 0:\", mod3 };\n");
	    printf("print \"%s: %ld * 2 ^ %ld - 1 is composite\";\n", program, orig_h, orig_n);
	} else {
	    printf("%ld * 2 ^ %ld - 1 is composite\n", orig_h, orig_n);
	}
	exit(1);
    }

    /*
     * NOTE: the values of h and n have been established and will not change thruout the test
     */
    fflush(stdout); // paranoia
    fflush(stderr); // paranoia
    dbg(DBG_LOW, "testing %lu*2^%lu-1", h, n);
    fflush(stderr); // paranoia

    /*
     * initialize prime stats for this run
     *
     * NOTE: This does not initialize prime stats for the total run.
     *	     The prime stats for the total run is setup when we either
     *	     restore from a checkpoint or determine the test is just starting.
     */
    initialize_beginrun_stats();

    /*
     * initialize prime stats for the start of this primality test
     *
     * If we restore from a checkpoint file later on,
     * after the checkpoint system has been initialixzed
     * and the most recent valid checkpont file is found,
     * we may modify the total stats as per that checkpoint.
     * If and until that happens, we will pretend the
     * total prime stats started with this run.
     */
    initialize_total_stats();

    /*
     * initialize mp elements
     */
    mpz_init(pow_2);
    mpz_init(h_pow_2);
    mpz_init(riesel_cand);
    mpz_init(u_term);
    mpz_init(u_term_sq);
    mpz_init(u_term_sq_2);
    mpz_init(J);
    mpz_init(K);
    mpz_init(J_div_h);
    mpz_init(J_mod_h);

    /*
     * initialize checkpoint system
     *
     * This does not perform a restore from q checkpoint file.
     * This just initializes internal data structures, sets up
     * the checkpoint timer and creates the checkpoint directory
     * if it it needed and does not exist.
     */
    initialize_checkpoint(checkpoint_dir, checkpoint_secs);

    /*
     * compute h*2^n-1 - our test candidate
     */
    mpz_ui_pow_ui(pow_2, 2, n);
    mpz_mul_ui(h_pow_2, pow_2, h);
    mpz_sub_ui(riesel_cand, h_pow_2, 1);
    if (debuglevel >= DBG_MED) {
	dbg(DBG_MED, "origianl test %lu*2^%lu-1\n", orig_h, orig_n);
	if (debuglevel >= DBG_HIGH) {
	    write_calc_mpz_hex(stderr, NULL, "riesel_cand", riesel_cand);
	}
	fflush(stderr); // paranoia
    }
    if (calc_mode) {
	printf("print \"original test %ld * 2 ^ %ld - 1\";\n", orig_h, orig_n);
	printf("print \"about to test %ld * 2 ^ %ld - 1\";\n", h, n);
	printf("riesel_cand = %ld * 2 ^ %ld - 1;\n", h, n);
	fflush(stdout); // paranoia
    }

    /*
     * firewall - h < 2^n
     */
    if (mpz_cmp_ui(pow_2, h) < 0) {
	err(12, __func__, "h: %lu must be < 2^n: 2^%lu\n", h, n);
	exit(12); // NOT REACHED
    }

    /*
     * set initial u(2) value
     */
    v1 = gen_u2(h, n, riesel_cand, u_term);
    if (debuglevel >= DBG_MED) {
	dbg(DBG_MED, "v[1] = %lu\n", v1);
	if (debuglevel >= DBG_HIGH) {
	    write_calc_mpz_hex(stderr, NULL, "u[2]", u_term);
	}
	fflush(stderr); // paranoia
    }
    if (calc_mode) {
	printf("print \"read lucas;\"\n");
	printf("read lucas;\n");
	printf("print \"v1 = gen_v1(%s, %s);\";\n", h_str, n_str);
	printf("v1 = gen_v1(%s, %s);\n", h_str, n_str);
	printf("print \"gmprime_v1 = %lu;\"\n", v1);
	printf("gmprime_v1 = %lu;\n", v1);
	printf("if (v1 == gmprime_v1) {\n");
	printf("  print \"v[1] value set correctly\";\n");
	printf("} else {\n");
	printf("  print \"# ERR: v1 != gmprime_v1\";\n");
	printf("  print \"v1 = \", v1;\n");
	printf("  print \"gmprime_v1 = \", gmprime_v1;\n");
	printf("  quit \"v[1] value not correctly set\";\n");
	printf("}\n");
	printf("v1 = gen_v1(%s, %s);\n", h_str, n_str);
	printf("print \"u_term = gen_u2(%s, %s, v1);\";\n", h_str, n_str);
	printf("u_term = gen_u2(%s, %s, v1);\n", h_str, n_str);
	write_calc_mpz_hex(stdout, NULL, "gmprime_u_term", u_term);
	printf("if (u_term == gmprime_u_term) {\n");
	printf("  print \"u[2] value set correctly\";\n");
	printf("} else {\n");
	printf("  print \"# ERR: u_term != gmprime_u_term for u[2]\";\n");
	printf("  print \"u_term = \", u_term;\n");
	printf("  print \"gmprime_u_term = \", gmprime_u_term;\n");
	printf("  quit \"u[2] value not correctly set\";\n");
	printf("}\n");
	fflush(stdout); // paranoia
    }

    /*
     * if checkpointing, perform an initial checkpoint for U(2)
     */
    if (checkpoint_dir != NULL) {
	checkpoint(checkpoint_dir, h, n, 2, u_term);
    }

    /*
     * compute u(n)
     *
     * u(i+1) = u(i)^2 - 2 mod 2^n-1
     */
    for (i = 2; i < n; ++i) {

	/*
	 * setup for next loop
	 */
	if (calc_mode) {
	    printf("print \"starting to compute u[%ld]\";\n", i);
	    write_calc_int64_t(stdout, NULL, "i", i);
	    fflush(stdout); // paranoia
	}

	/*
	 * square
	 */
	mpz_mul(u_term_sq, u_term, u_term);
	if (debuglevel >= DBG_HIGH) {
	    write_calc_mpz_hex(stderr, NULL, "u_term_sq", u_term_sq);
	    fflush(stderr); // paranoia
	}
	if (calc_mode) {
	    printf("u_term_sq = u_term^2;\n");
	    write_calc_mpz_hex(stdout, NULL, "gmprime_u_term_sq", u_term_sq);
	    printf("if (u_term_sq == gmprime_u_term_sq) {\n");
	    printf("  print \"gmprime_u_term_sq appears to be correct\";\n");
	    printf("} else {\n");
	    printf("  print \"# ERR: u_term_sq != gmprime_u_term_sq for u[%ld]\";\n", i);
	    printf("  print \"u_term_sq = \", u_term_sq;\n");
	    printf("  print \"gmprime_u_term_sq = \", gmprime_u_term_sq;\n");
	    printf("  quit \"bad square calculation\";\n");
	    printf("}\n");
	    fflush(stdout); // paranoia
	}

	/*
	 * -2
	 */
	mpz_sub_ui(u_term_sq_2, u_term_sq, (unsigned long int) 2);
	if (debuglevel >= DBG_HIGH) {
	    write_calc_mpz_hex(stderr, NULL, "u_term_sq_2", u_term_sq_2);
	    fflush(stderr); // paranoia
	}
	if (calc_mode) {
	    printf("u_term_sq_2 = u_term_sq - 2;\n");
	    write_calc_mpz_hex(stdout, NULL, "gmprime_u_term_sq_2", u_term_sq_2);
	    printf("if (u_term_sq_2 == gmprime_u_term_sq_2) {\n");
	    printf("  print \"gmprime_u_term_sq_2 appears to be correct\";\n");
	    printf("} else {\n");
	    printf("  print \"# ERR: u_term_sq_2 != gmprime_u_term_sq_2 for u[%ld]\";\n", i);
	    printf("  print \"u_term_sq_2 = \", u_term_sq_2;\n");
	    printf("  print \"gmprime_u_term_sq_2 = \", gmprime_u_term_sq_2;\n");
	    printf("  quit \"bad -2 calculation\";\n");
	    printf("}\n");
	    fflush(stdout); // paranoia
	}

	/*
	 * mod h*2^n-1 via modified "shift and add"
	 *
	 * See http://www.isthe.com/chongo/tech/math/prime/prime-tutorial.pdf
	 * for the page entitled "Calculating mod h*2n-1".
	 *
	 * Executive summary:
	 *
	 *      u_term = u_term_sq_2 mod h*2^n-1 = int(J/h) + (J mod h)*(2^n) + K
	 *
	 * Where:
	 *
	 *      J = int(u_term_sq_2 / 2^n)      // u_term_sq_2 right shifted by n bits
	 *      K = u_term_sq_2 mod 2^n         // the bottom n bits of u_term_sq_2
	 *
	 * NOTE: We use 2^n above to mean 2 raised to the power of n, not xor.
	 */
	mpz_fdiv_q_2exp(J, u_term_sq_2, n);	// J = int(u_term_sq_2 / 2^n)
	if (debuglevel >= DBG_VHIGH) {
	    write_calc_mpz_hex(stderr, NULL, "J", J);
	    fflush(stderr); // paranoia
	}
	mpz_tdiv_qr_ui(J_div_h, J_mod_h, J, h);	// compute both int(J/h) and (J mod h)
	if (debuglevel >= DBG_VHIGH) {
	    write_calc_mpz_hex(stderr, NULL, "J_div_h", J_div_h);
	    write_calc_mpz_hex(stderr, NULL, "J_mod_h", J_mod_h);
	    fflush(stderr); // paranoia
	}
	mpz_mul_2exp(J_mod_h, J_mod_h, n);	// (J mod h)*(2^n)
	if (debuglevel >= DBG_VHIGH) {
	    write_calc_mpz_hex(stderr, NULL, "J_mod_h_shifted", J_mod_h);
	    fflush(stderr); // paranoia
	}
	mpz_fdiv_r_2exp(K, u_term_sq_2, n);	// K = bottom n bits of u_term_sq_2
	if (debuglevel >= DBG_VHIGH) {
	    write_calc_mpz_hex(stderr, NULL, "K", K);
	    fflush(stderr); // paranoia
	}
	mpz_add(u_term, J_mod_h, K);	// int(J/h) + (J mod h)*(2^n)
	if (debuglevel >= DBG_VHIGH) {
	    write_calc_mpz_hex(stderr, NULL, "u_term_partial", u_term);
	    fflush(stderr); // paranoia
	}
	mpz_add(u_term, u_term, J_div_h);	// u_term = u_term_sq_2 mod h*2^n-1
	if (debuglevel >= DBG_HIGH) {
	    write_calc_mpz_hex(stderr, NULL, "u_term_mod_final", u_term);
	    fflush(stderr); // paranoia
	}

	/*
	 * While the above modified "shift and add" does compute u_term_sq_2 mod h*2^n-1
	 * it can produce a value that is >= h*2^n-1 in some extreme cases.  When that
	 * happens, the value will be slightly larger than h*2^n-1.  In particular it
	 * will be bounded under an upper bound that we derive below.
	 *
	 * Assume:
	 *
	 *      hb = the number of bits in h, which for this C code is 64 bits
	 *
	 * We know that:
	 *      rb = the number of bits in h*2^n-1 (our riesel_cand), for this C code is hb + n
	 *      u2b = the number of bits in (h*2^n-1)^2, for this C code is 2*rb = 2*hb + 2*n
	 *
	 * Now:
	 *
	 *      u_term = u_term_sq_2 mod h*2^n-1 = int(J/h) + (J mod h)*(2^n) + K
	 *
	 * Where:
	 *
	 *      J = int(u_term_sq_2 / 2^n)      // u_term_sq_2 right shifted by n bits
	 *      K = u_term_sq_2 mod 2^n         // the bottom n bits of u_term_sq_2
	 *
	 * We need to determine the sizes of the terms used to compute the new u_term.
	 * It is easy to show that:
	 *
	 *      jb = the number of bits in J = (2*hb + 2*n) - n = 2*hb + n
	 *      jdhb = the number of bits in int(J/h) = jb - hb = 2*hb + n - hb = hb + n
	 *      jmhb = the number of bits in (J mod h)*(2^n) = hb + n
	 *
	 *      kb = the number of bits in K = n
	 *
	 * Then it is easy to show that the size of the new u_term in bits is as most:
	 *
	 *      max(max(jdhb, jmhb)+1, n) = max(max(hb + n, hb + n)+1, n) = max(hb + n + 1, n) = hb + n + 1
	 *
	 *        (The reason for the + 1 in the above expression is due to a potential carry bit.)
	 *
	 * Therefore the new u_term in bits is at most twice h*2^n-1, our riesel_cand.  So we
	 * when the new u_term > riesel_cand, we expect to subtract riesel_cand at most one time.
	 */
	while (mpz_cmp(u_term, riesel_cand) >= 0) {
	    mpz_sub(u_term, u_term, riesel_cand);
	    if (debuglevel >= DBG_HIGH) {
		write_calc_mpz_hex(stderr, NULL, "u_term_subtract", u_term);
		fflush(stderr); // paranoia
	    }
	}
	if (debuglevel >= DBG_MED) {
	    write_calc_mpz_hex(stderr, NULL, "u_term", u_term);
	    fflush(stderr); // paranoia
	}
	if (calc_mode) {
	    printf("u_term = u_term_sq_2 %% riesel_cand;\n");
	    write_calc_mpz_hex(stdout, NULL, "gmprime_u_term", u_term);
	    printf("if (u_term == gmprime_u_term) {\n");
	    printf("  print \"gmprime_u_term appears to be correct\";\n");
	    printf("} else {\n");
	    printf("  print \"# ERR: u_term_sq_2 != gmprime_u_term for u[%ld]\";\n", i);
	    printf("  print \"u_term = \", u_term;\n");
	    printf("  print \"gmprime_u_term = \", gmprime_u_term;\n");
	    printf("  quit \"bad mod calculation\";\n");
	    printf("}\n");
	    fflush(stdout); // paranoia
	}
    }
    dbg(DBG_LOW, "finished testing %lu*2^%lu-1", h, n);
    fflush(stderr); // paranoia

    /*
     * print final prime stats according to -t and/or -T
     */
    if (write_stats) {
	update_stats();
	write_calc_prime_stats(stderr, write_extended_stats);
    }

    /*
     * h*2^n-1 is prime if and only if u(n) == 0
     */
    if (calc_mode) {
	printf("print \"%s: u[%ld] =\", u_term;\n", program, i);
	printf("print \"%s: original test: %ld * 2 ^ %ld - 1;\"\n", program, orig_h, orig_n);
	printf("print \"%s: actual test: %ld * 2 ^ %ld - 1;\"\n", program, h, n);
    }
    if (mpz_sgn(u_term) == 0) {
	if (calc_mode) {
	    printf("if (u_term == 0) { print \"u[%ld] == 0\"; } else { print \"ERROR: u[%ld] != 0\"; }\n", i, i);
	    printf("print \"%s: %ld * 2 ^ %ld - 1 is prime\";\n", program, orig_h, orig_n);
	} else {
	    printf("%ld * 2 ^ %ld - 1 is prime\n", orig_h, orig_n);
	}
    } else {
	if (calc_mode) {
	    printf("if (u_term != 0) { print \"u[%ld] != 0\"; } else { print \"ERROR: u[%ld] != 0\"; }\n", i, i);
	    printf("print \"%s: %ld * 2 ^ %ld - 1 is composite\";\n", program, orig_h, orig_n);
	} else {
	    printf("%ld * 2 ^ %ld - 1 is composite\n", orig_h, orig_n);
	}
	exit(1);
    }

    /*
     * All Done!! -- Jessica Noll, Age 2
     */
    exit(0);
}
