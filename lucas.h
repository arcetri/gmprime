/*
 * lucas - implementaion of calc's lucas.cal in C using gmp
 *
 * This code was converted by Konstantin Simeonov from the lucas.cal
 * calc resource file as distrivurted by calc in version 2.12.6.7.
 * For information on calc, see:
 *
 * 	http://www.isthe.com/chongo/tech/comp/calc/index.html
 * 	https://github.com/lcn2/calc
 *
 * For information on lucas.cal see:
 *
 * 	https://github.com/lcn2/calc/blob/master/cal/lucas.cal
 *
 * For a general tutorial on how to find a new largest known prime, see:
 *
 *	http://www.isthe.com/chongo/tech/math/prime/prime-tutorial.pdf
 *
 * Credit for C/gmp implemention: Konstantin Simeonov
 * Credit for the original lucas.cal calc implementation: Landon Curt Noll
 *
 * Copyright (c) 2018 by Konstantin Simeonov and Landon Curt Noll.  All Rights Reserved.
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

#ifndef GEN_U2_LIBRARY_H
#define GEN_U2_LIBRARY_H

#include <stdint.h>
#include <gmp.h>

/*
 * Error codes
 */
#define LT_ONE_H       1    /* h is < 1  */
#define EVEN_H         2    /* h is even */
#define LT_ONE_N       4    /* n is < 1  */
#define LT_THREE_V1    8    /* v1 is < 3 */
#define H_N_RULE_VIOL 16    /* h and n violate the rule 0 < h < n */

/*
 * The library function call to generate the U_0 (first term) of the Lucas sequence of a candidate h*2^n-1.
 * input:
 *      h  - the value of h
 *      n  - the value of n
 *      u2 - the U term variable to be set by the function once U_0 is generated successfully
 *
 * returns:
 *      0 - Success
 *   != 0 - See error codes above
 */
uint8_t gen_u0(uint64_t h, uint64_t n, mpz_t u2);

#endif /* GEN_U2_LIBRARY_H */
