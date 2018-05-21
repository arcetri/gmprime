/*
 * gmprime - gmp h*2^n-1 Riesel primality tester
 *
 * usage:
 *
 *	gmprime h n
 *
 *	h	power of 2 multuplier (as in h*2^n-1)
 *	n	power of 2 (as in h*2^n-1)
 *
 * NOTE: Sometimes u(2) is called u(0).
 *
 * NOTE: This is for demo purposes only.  One should certailny improve
 *	 the robustness and performance of this code!!!
 *
 *	 For more information on calc, see:
 *
 *	     http://www.isthe.com/chongo/tech/comp/calc/index.html
 *
 *	 The source to calc is freely available via links from the above URL.
 *
 * Exit codes:
 *
 *	0	h*2^n-1 is prime (also prints "prime" to stdout)
 *	1	h*2^n-1 is not prime (also prints "composite" to stdout)
 *	>1	some error occurred
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

/* constants */
#define MAX_H_N_LEN BUFSIZ	/* more than enougn for h and n that we care about */

/* globals */ char *program;	      /* our name */
static const char *usage = "[-v] [-c x] h n\n"
    "\n"
    "\t-v\tverbose mode\n"
    "\t-c\toutput to stdout, calc code that may be used to verify partial results\n"
    "\n"
    "\th\tpower of 2 multuplier (as in h*2^n-1) must be > 0 and < 2^n\n"
    "\tn\tpower of 2 (as in h*2^n-1) must be > 0\n";

/* list of very small verified Riesel primes that we special case */
struct h_n {
    unsigned long h;	/* multiplier of 2 */
    unsigned long n;	/* power of 2 */
};
static const struct h_n small_h_n[] = {
    {1, 2},	/* 1 * 2 ^ 2 - 1 = 3 is prime */

    {0, 0}	/* MUST BE THE LAST ENTRY! */
};
static const struct h_n composite_h_n[] = {
    {1, 1},	/* 1 * 2 ^ 1 - 1 = 1 is not prime */

    {0, 0}	/* MUST BE THE LAST ENTRY! */
};


/*
 * test h*2^n-1 for primality
 */
int
main(int argc, char *argv[])
{
    char *h_arg;	/* h as a string */
    char *n_arg;	/* n as a string */
    int ret;		/* snprintf error return */
    mpz_t pow_2;	/* 2^n */
    mpz_t h_pow_2;	/* h*(2^n) */
    mpz_t riesel_cand;	/* Riesel candidate to test - n*(2^n)-1 */
    mpz_t u_term;	/* Riesel sequence value */
    mpz_t u_term_sq;	/* square of prev term */
    mpz_t u_term_sq_2;	/* square - 2 of prev term */
    mpz_t J;            /* used in mod calculation - u_term_sq_2 / (2^n) */
    mpz_t K;            /* used in mod calculation - u_term_sq_2 mod (2^n) */
    mpz_t J_div_h;      /* used in mod calculation - int(J/h) */
    mpz_t J_mod_h;      /* used in mod calculation - J mod h then (J mod h)*(2^n) */
    int c;		/* option */
    unsigned long i;	/* u term index */
    unsigned long h;	/* multiplier of 2 */
    unsigned long n;	/* power of 2 */
    unsigned long orig_h;	/* original value of h */
    unsigned long orig_n;	/* original valye of n */
    char h_str[MAX_H_N_LEN+1];	/* h as a string */
    char n_str[MAX_H_N_LEN+1];	/* h as a string */
    int h_len;			/* length of string in h_str */
    int n_len;			/* length of string in n_str */
    const struct h_n *h_n_p;	/* pointer into small_h_n */
    int verbose = 0;		/* be verbose */
    int calc_mode = 0;		/* output calc code so calc can verify partial results */
    extern int optind;		/* argv index of the next arg */

    /*
     * parse args
     */
    program = argv[0];
    while ((c = getopt(argc, argv, "vc")) != -1) {
	switch (c) {
	case 'v':
	    verbose = 1;
	    break;
	case 'c':
	    calc_mode = 1;
	    break;
	default:
	    fprintf(stderr, "usage: %s %s", program, usage);
	    exit(2);
	}
    }
    argv += (optind - 1);
    argc -= (optind - 1);
    if (argc != 3) {
	fprintf(stderr, "usage: %s %s", program, usage);
	exit(3);
    }
    h_arg = argv[1];
    errno = 0;
    h = strtoul(h_arg, NULL, 0);
    if (strchr(h_arg, '-') != NULL || errno != 0 || h <= 0) {
	fprintf(stderr, "%s: FATAL: h must an integer > 0\n", program);
	fprintf(stderr, "usage: %s %s", program, usage);
	exit(4);
    }
    n_arg = argv[2];
    errno = 0;
    n = strtoul(n_arg, NULL, 0);
    if (strchr(n_arg, '-') != NULL || errno != 0 || n <= 0) {
	fprintf(stderr, "%s: FATAL: n must an integer > 0\n", program);
	fprintf(stderr, "usage: %s %s", program, usage);
	exit(5);
    }

    /*
     * convert even h into odd h by increasing n
     */
    /* save our argument values for debugging and final reporting */
    orig_h = h;
    orig_n = n;
    /* force h to become odd */
    if (h % 2 == 0) {
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: converting even h: %ld into odd by increasing n: %ld\n", program, orig_h, orig_n);
        }
	while (h % 2 == 0 && h > 0) {
	    h >>= 1;
	    ++n;
	}
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: new equivalent h: %lu and new equivalent n: %ld\n", program, h, n);
        }
	if (h <= 0) {
	    fprintf(stderr, "%s: FATAL: new equivalent h: %lu <= 0\n", program, h);
	    exit(6);
	}
    }
    /* form string based on possibly modified h */
    h_str[0] = '\0'; // paranoia
    h_str[MAX_H_N_LEN] = '\0'; // paranoia
    h_len = snprintf(h_str, MAX_H_N_LEN, "%lu", h);
    if (h_len < 0 || h_len >= MAX_H_N_LEN) {
	fprintf(stderr, "%s: FATAL: converting h: %lu to string via snprintf returned: %d\n", program, h, h_len);
	exit(6);
    }
    h_str[h_len] = '\0';	// paranoia
    /* form string based on possibly modified n */
    n_str[0] = '\0'; // paranoia
    n_str[MAX_H_N_LEN] = '\0'; // paranoia
    n_len = snprintf(n_str, MAX_H_N_LEN, "%lu", n);
    if (n_len < 0 || n_len >= MAX_H_N_LEN) {
	fprintf(stderr, "%s: FATAL: converting n: %lu to string via snprintf returned: %d\n", program, n, n_len);
	exit(7);
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
     * 		h = 1 mod 3 AND n is even
     * or when:
     *		h = 2 mod 3 AND n is odd
     *
     * If either of those cases is true, don't test for
     * primality because the value is a multiple of 3.
     * We also know that h*2^n-1 is not 3 because the
     * 'catch the special cases for small primes' code
     * would have exited above if h*2^n-1 == 3.
     */
    if (((h % 3 == 1) && (n % 2) == 0) || ((h % 3 == 2) && (n % 2 == 1))) {
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
     * compute h*2^n-1 - our test candidate
     */
    mpz_ui_pow_ui(pow_2, 2, n);
    mpz_mul_ui(h_pow_2, pow_2, h);
    mpz_sub_ui(riesel_cand, h_pow_2, 1);
    if (verbose) {
	fprintf(stderr, "%s: DEBUG: origianl test %lu*2^%lu-1\n", program, orig_h, orig_n);
	fprintf(stderr, "%s: DEBUG: testing %lu*2^%lu-1 = ", program, h, n);
	mpz_out_str(stderr, 10, riesel_cand);
	fputc('\n', stderr);
    }
    if (calc_mode) {
	printf("print \"original test %ld * 2 ^ %ld - 1\";\n", orig_h, orig_n);
	printf("print \"about to test %ld * 2 ^ %ld - 1\";\n", h, n);
	printf("riesel_cand = %ld * 2 ^ %ld - 1;\n", h, n);
    }

    /*
     * firewall - h < 2^n
     */
    if (mpz_cmp_ui(pow_2, h) < 0) {
	fprintf(stderr, "%s: FATAL: h: %lu must be < 2^n: 2^%lu\n", program, h, n);
	exit(8);
    }

    /*
     * set initial u(2) value
     */
    if ((ret = gen_u0(h, n, u_term)) != 0) {
	fprintf(stderr, "%s: FATAL: failed to generate u[2], gen_u0() exited with code: %d\n", program, ret);
	exit(9);
    }
    if (verbose) {
	fprintf(stderr, "%s: DEBUG: u[2] = ", program);
	mpz_out_str(stderr, 10, u_term);
	fputc('\n', stderr);
    }
    if (calc_mode) {
	printf("print \"read lucas;\"\n");
	printf("read lucas;\n");
	printf("print \"u_term = gen_u0(%s, %s, gen_v1(%s, %s));\";\n", h_str, n_str, h_str, n_str);
	printf("u_term = gen_u0(%s, %s, gen_v1(%s, %s));\n", h_str, n_str, h_str, n_str);
	printf("gmprime_u_term = ");
	mpz_out_str(stdout, 10, u_term);
	printf(";\n");
	printf("if (u_term == gmprime_u_term) {\n");
	printf("  print \"u[2] value set correctly\";\n");
	printf("} else {\n");
	printf("  print \"# ERR: u_term != gmprime_u_term for u[2]\";\n");
	printf("  print \"u_term = \", u_term;\n");
	printf("  print \"gmprime_u_term = \", gmprime_u_term;\n");
	printf("  quit \"u[2] value not correctly set\";\n");
	printf("}\n");
    }

    /*
     * compute u(n)
     *
     * u(i+1) = u(i)^2 - 2 mod 2^n-1
     */
    for (i=2; i < n; ++i) {

	/* setup for next loop */
	if (calc_mode) {
	    printf("print \"starting to compute u[%ld]\";\n", i);
	}

    	/* square */
	mpz_mul(u_term_sq, u_term, u_term);
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: u[%ld]^2 = ", program, i);
	    mpz_out_str(stderr, 10, u_term_sq);
	    fputc('\n', stderr);
	}
	if (calc_mode) {
	    printf("u_term_sq = u_term^2;\n");
	    printf("gmprime_u_term_sq = ");
	    mpz_out_str(stdout, 10, u_term_sq);
	    printf(";\n");
	    printf("if (u_term_sq == gmprime_u_term_sq) {\n");
	    printf("  print \"gmprime_u_term_sq appears to be correct\";\n");
	    printf("} else {\n");
	    printf("  print \"# ERR: u_term_sq != gmprime_u_term_sq for u[%ld]\";\n", i);
	    printf("  print \"u_term_sq = \", u_term_sq;\n");
	    printf("  print \"gmprime_u_term_sq = \", gmprime_u_term_sq;\n");
	    printf("  quit \"bad square calculation\";\n");
	    printf("}\n");
	}

	/* -2 */
	mpz_sub_ui(u_term_sq_2, u_term_sq, (unsigned long int)2);
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: u[%ld]^2-2 = ", program, i);
	    mpz_out_str(stderr, 10, u_term_sq_2);
	    fputc('\n', stderr);
	}
	if (calc_mode) {
	    printf("u_term_sq_2 = u_term_sq - 2;\n");
	    printf("gmprime_u_term_sq_2 = ");
	    mpz_out_str(stdout, 10, u_term_sq_2);
	    printf(";\n");
	    printf("if (u_term_sq_2 == gmprime_u_term_sq_2) {\n");
	    printf("  print \"gmprime_u_term_sq_2 appears to be correct\";\n");
	    printf("} else {\n");
	    printf("  print \"# ERR: u_term_sq_2 != gmprime_u_term_sq_2 for u[%ld]\";\n", i);
	    printf("  print \"u_term_sq_2 = \", u_term_sq_2;\n");
	    printf("  print \"gmprime_u_term_sq_2 = \", gmprime_u_term_sq_2;\n");
	    printf("  quit \"bad -2 calculation\";\n");
	    printf("}\n");
	}

	/*
	 * mod h*2^n-1 via modified "shift and add"
	 *
	 * See http://www.isthe.com/chongo/tech/math/prime/prime-tutorial.pdf
	 * for the page entitled "Calculating mod h*2n-1".
	 *
	 * Executive summary:
	 *
	 *	u_term = u_term_sq_2 mod h*2^n-1 = int(J/h) + (J mod h)*(2^n) + K
	 *
	 * Where:
	 *
	 *	J = int(u_term_sq_2 / 2^n)	// u_term_sq_2 right shifted by n bits
	 *	K = u_term_sq_2 mod 2^n 	// the bottom n bits of u_term_sq_2
	 *
	 * NOTE: We use 2^n above to mean 2 raised to the power of n, not xor.
	 */
	mpz_fdiv_q_2exp(J, u_term_sq_2, n);	// J = int(u_term_sq_2 / 2^n)
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: J = ", program);
	    mpz_out_str(stderr, 10, J);
	    fputc('\n', stderr);
        }
	mpz_tdiv_qr_ui(J_div_h, J_mod_h, J, h);	// compute both int(J/h) and (J mod h)
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: int(J/h) = ", program);
	    mpz_out_str(stderr, 10, J_div_h);
	    fputc('\n', stderr);
	    fprintf(stderr, "%s: DEBUG: (J mod h) = ", program);
	    mpz_out_str(stderr, 10, J_mod_h);
	    fputc('\n', stderr);
        }
	mpz_mul_2exp(J_mod_h, J_mod_h, n);		// (J mod h)*(2^n)
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: (J mod h)*(2^n) = ", program);
	    mpz_out_str(stderr, 10, J_mod_h);
	    fputc('\n', stderr);
        }
	mpz_fdiv_r_2exp(K, u_term_sq_2, n);	// K = bottom n bits of u_term_sq_2
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: K = ", program);
	    mpz_out_str(stderr, 10, K);
	    fputc('\n', stderr);
        }
	mpz_add(u_term, J_mod_h, K);		// int(J/h) + (J mod h)*(2^n)
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: int(J/h) + (J mod h)*(2^n) = ", program);
	    mpz_out_str(stderr, 10, u_term);
	    fputc('\n', stderr);
        }
	mpz_add(u_term, u_term, J_div_h);	// u_term = u_term_sq_2 mod h*2^n-1
	if (verbose) {
	    fprintf(stderr, "%s: DEBUG: u_term = u_term_sq_2 mod h*2^n-1 = ", program);
	    mpz_out_str(stderr, 10, u_term);
	    fputc('\n', stderr);
        }

	/*
	 * While the above modified "shift and add" does compute u_term_sq_2 mod h*2^n-1
	 * it can produce a value that is >= h*2^n-1 in some extreme cases.  When that
	 * happens, the value will be slightly larger than h*2^n-1.  In particular it
	 * will be bounded under an upper bound that we derive below.
	 *
	 * Assume:
	 *
	 *	hb = the number of bits in h, which for this C code is 64 bits
	 *
	 * We know that:
	 *	rb = the number of bits in h*2^n-1 (our riesel_cand), for this C code is hb + n
	 *	u2b = the number of bits in (h*2^n-1)^2, for this C code is 2*rb = 2*hb + 2*n
	 *
	 * Now:
	 *
	 *	u_term = u_term_sq_2 mod h*2^n-1 = int(J/h) + (J mod h)*(2^n) + K
	 *
	 * Where:
	 *
	 *	J = int(u_term_sq_2 / 2^n)	// u_term_sq_2 right shifted by n bits
	 *	K = u_term_sq_2 mod 2^n 	// the bottom n bits of u_term_sq_2
	 *
	 * We need to determine the sizes of the terms used to compute the new u_term.
	 * It is easy to show that:
	 *
	 *	jb = the number of bits in J = (2*hb + 2*n) - n = 2*hb + n
	 *	jdhb = the number of bits in int(J/h) = jb - hb = 2*hb + n - hb = hb + n
	 *	jmhb = the number of bits in (J mod h)*(2^n) = hb + n
	 *
	 *	kb = the number of bits in K = n
	 *
	 * Then it is easy to show that the size of the new u_term in bits is as most:
	 *
	 *	max(max(jdhb, jmhb)+1, n) = max(max(hb + n, hb + n)+1, n) = max(hb + n + 1, n) = hb + n + 1
	 *
	 *	  (The reason for the + 1 in the above expression is due to a potential carry bit.)
	 *
	 * Therefore the new u_term in bits is at most twice h*2^n-1, our riesel_cand.  So we
	 * when the new u_term > riesel_cand, we expect to subtract riesel_cand at most one time.
	 */
	while (mpz_cmp(u_term, riesel_cand) >= 0) {
	    mpz_sub(u_term, u_term, riesel_cand);
	    if (verbose) {
		fprintf(stderr, "%s: DEBUG: u_term = u_term - h*2^n-1 = ", program);
		mpz_out_str(stderr, 10, u_term);
		fputc('\n', stderr);
	    }
	}
	if (calc_mode) {
	    printf("u_term = u_term_sq_2 %% riesel_cand;\n");
	    printf("gmprime_u_term = ");
	    mpz_out_str(stdout, 10, u_term);
	    printf(";\n");
	    printf("if (u_term == gmprime_u_term) {\n");
	    printf("  print \"gmprime_u_term appears to be correct\";\n");
	    printf("} else {\n");
	    printf("  print \"# ERR: u_term_sq_2 != gmprime_u_term for u[%ld]\";\n", i);
	    printf("  print \"u_term = \", u_term;\n");
	    printf("  print \"gmprime_u_term = \", gmprime_u_term;\n");
	    printf("  quit \"bad mod calculation\";\n");
	    printf("}\n");
	}
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
