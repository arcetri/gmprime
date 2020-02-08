/*
 * riesel - implementaion of calc's setup functions in lucas.cal in C using gmp
 *
 * This code was converted by Konstantin Simeonov from the lucas.cal
 * calc resource file as distrivurted by calc in version 2.12.6.7.
 * For information on calc, see:
 *
 *      http://www.isthe.com/chongo/tech/comp/calc/index.html
 *      https://github.com/lcn2/calc
 *
 * For information on lucas.cal see:
 *
 *      https://github.com/lcn2/calc/blob/master/cal/lucas.cal
 *
 * For a general tutorial on how to find a new largest known prime, see:
 *
 *      http://www.isthe.com/chongo/tech/math/prime/prime-tutorial.pdf
 *
 * Credit for C/gmp implemention: Konstantin Simeonov
 * Credit for the original lucas.cal calc implementation: Landon Curt Noll
 *
 * Copyright (c) 2018,2020 by Konstantin Simeonov and Landon Curt Noll.  All Rights Reserved.
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
 * Share and enjoy! :-)
 */

#if !defined(INCLUDE_RIESEL_H)
#define INCLUDE_RIESEL_H

#include <stdint.h>
#include <gmp.h>

/*
 * NOTE: In some litature they use U(0) or U(1) as the first term.
 * 	 We use U(2) so that U(N) is the critical value.  I.e., the
 * 	 primality of h*2^n-1 depends in U(N) being a multiple of h*2^n-1.
 */
#define FIRST_TERM_INDEX (2)	// first Lucas term is U(2), so first index is 2

/*
 * external functions
 */
extern unsigned long gen_u2(uint64_t h, uint64_t n, mpz_t riesel_cand, mpz_t u2);
extern unsigned long gen_v1(uint64_t h, uint64_t n, mpz_t riesel_cand);

#endif				/* INCLUDE_RIESEL_H */
