/*
 * lucas - implementaion of calc's lucas.cal in C using gmp
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

#include <stdio.h>
#include <limits.h>
#include "lucas.h"

/*
 * A macro that checks if a number is odd (return true) or not (return false)
 */
#define IS_ODD(NUMBER) \
        ((NUMBER) & 1ULL)

/*
 * A macro that determines whether a given binary bit is set in a value
 */
#define TEST_BIT(val, bit) \
        ((val) & (1ULL << (bit)))

/*
 * A macro that finds the highest bit of an 64bit integer
 */
#define HIGHBIT(h) \
	((sizeof(h) * CHAR_BIT - 1) - __builtin_clzll(h))

/*
 * The table with most probable X values for the lucas sequence.
 *
 * For information on the x_tbl[] array and next_x, see:
 *
 *      http://www.isthe.com/chongo/tech/math/prime/prime-tutorial.pdf
 *
 * See the page titled: "How to find V(1) when h is a multiple of 3" (around page 85)
 * and the page titled: "How to find V(1) when h is NOT a multiple of 3" (around page 86).
 */
#define X_TBL_LEN 42U
static const unsigned long x_tbl[X_TBL_LEN] = {
    3, 5, 9, 11, 15, 17, 21, 29, 27, 35, 39, 41, 31, 45, 51, 55, 49, 59, 69, 65, 71, 57, 85, 81,
    95, 99, 77, 53, 67, 125, 111, 105, 87, 129, 101, 83, 165, 155, 149, 141, 121, 109
};
/*
 * The next probable X value if the table does not satisfy the requirements
 */
static const uint8_t next_x = 167U;

/*
 * static function declarations
 */
static int rodseth_xhn(uint32_t x, mpz_t riesel_cand);


/*
 * gen_u2 - determine the initial Lucas sequence for h*2^n-1
 *
 * Historically many start the Lucas sequence with u(0).
 * Some, like the author of this code, prefer to start
 * with U(2).  This is so one may say:
 *
 *      2^p-1 is prime if u(p) = 0 mod 2^p-1
 * or:
 *      h*2^p-1 is prime if u(p) = 0 mod h*2^p-1
 *
 * According to Ref1, Theorem 5:
 *
 *      u(2) = alpha^h + alpha^(-h)     (NOTE: Ref1 calls it u(0))
 *
 * Now:
 *
 *      v(x) = alpha^x + alpha^(-x)     (Ref1, bottom of page 872)
 *
 * Therefore:
 *
 *      u(2) = v(h)                     (NOTE: Ref1 calls it u(0))
 *
 * We calculate v(h) as follows:        (Ref1, top of page 873)
 *
 *      v(0) = alpha^0 + alpha^(-0) = 2
 *      v(1) = alpha^1 + alpha^(-1) = gen_v1(h,n)
 *      v(n+2) = v(1)*v(n+1) - v(n)
 *
 * This function does not concern itself with the value of 'alpha'.
 * The gen_v1() function is used to compute v(1), and identity
 * functions take it from there.
 *
 * It can be shown that the following are true:
 *
 *      v(2*n) = v(n)^2 - 2
 *      v(2*n+1) = v(n+1)*v(n) - v(1)
 *
 * To prevent v(x) from growing too large, one may replace v(x) with
 * `v(x) mod h*2^n-1' at any time.
 *
 * See the function gen_v1() for details on the value of v(1).
 *
 * input:
 *      h               h as in h*2^n-1       (must be >= 1)
 *      n               n as in h*2^n-1       (must be >= 1)
 *      riesel_cand     pre-computed h*2^n-1 as an mpz_t
 *      u(2)            initial value for Lucas test on h*2^n-1
 *
 * returns:
 *      v(1) used to compute u(2)
 */
unsigned long
gen_u2(uint64_t h, uint64_t n, mpz_t riesel_cand, mpz_t u2)
{
    unsigned long v1;		/* v(1) based on h and n */
    uint8_t hbits;		/* highest bit set in h */
    uint8_t i;			/* counter */
    mpz_t r;			/* low value: v(n) */
    mpz_t s;			/* high value: v(n+1) */
    mpz_t tmp;			/* Placeholder for some GNUMP values */

    /*
     * compute v(1)
     */
    v1 = gen_v1(h, n, riesel_cand);

    /*
     * Initialize the GNUMP variables
     */
    mpz_init(tmp);
    mpz_init(r);
    mpz_init(s);

    /*
     * build up u2 based on the reversed bits of h
     */
    hbits = HIGHBIT(h);

    /*
     * setup for bit loop r = v1
     */
    mpz_set_ui(r, v1);

    /*
     * s = r^2 - 2
     */
    mpz_set(s, r);
    mpz_mul(s, s, s);
    mpz_sub_ui(s, s, 2ULL);

    /*
     * deal with small h as a special case
     *
     * The h value is odd > 0, and it needs to be
     * at least 2 bits long for the loop below to work.
     * TODO: Replace the mpz_mod everywhere with shift operations.
     * NOTE: In GNUMP the speed increase of the shift operations opposed to the usual mpz_mod is minimal
     */
    if (h == 1) {
	/*
	 * return r%(h*2^n-1);
	 */
	mpz_mod(tmp, r, riesel_cand);
	mpz_set(u2, tmp);
	return v1;
    }

    /*
     * cycle from second highest bit to second lowest bit of h
     */
    for (i = hbits - (uint8_t) 1; i > 0; --i) {

	/*
	 * bit(i) is 1
	 */
	if (TEST_BIT(h, i)) {

	    /*
	     * compute v(2n+1) = v(r+1)*v(r)-v1
	     */
	    /*
	     * r = (r*s - v1) % (h*2^n-1);
	     */
	    mpz_mul(tmp, r, s);
	    mpz_sub_ui(tmp, tmp, v1);
	    mpz_mod(r, tmp, riesel_cand);

	    /*
	     * compute v(2n+2) = v(r+1)^2-2
	     */
	    /*
	     * s = (s^2 - 2) % (h*2^n-1);
	     */
	    mpz_mul(s, s, s);
	    mpz_sub_ui(s, s, 2ULL);
	    mpz_mod(s, s, riesel_cand);

	    /*
	     * bit(i) is 0
	     */
	} else {

	    /*
	     * compute v(2n+1) = v(r+1)*v(r)-v1
	     */
	    /*
	     * s = (r*s - v1) % (h*2^n-1);
	     */
	    mpz_mul(tmp, r, s);
	    mpz_sub_ui(tmp, tmp, v1);
	    mpz_mod(s, tmp, riesel_cand);

	    /*
	     * compute v(2n) = v(r)^-2
	     */
	    /*
	     * r = (r^2 - 2) % (h*2^n-1);
	     */
	    mpz_mul(r, r, r);
	    mpz_sub_ui(r, r, 2ULL);
	    mpz_mod(r, r, riesel_cand);
	}
    }

    /*
     * we know that h is odd, so the final bit(0) is 1
     */
    /*
     * r = (r*s - v1) % (h*2^n-1);
     */
    mpz_mul(tmp, r, s);
    mpz_sub_ui(tmp, tmp, v1);
    mpz_mod(r, tmp, riesel_cand);

    /*
     * compute the final u2 return value
     */
    mpz_set(u2, r);

    /*
     * free the GNUMP variables and return success
     */
    mpz_clear(r);
    mpz_clear(s);
    mpz_clear(tmp);
    return v1;
}


/*
 * gen_v1 - compute the v(1) for a given h*2^n-1 if we can
 *
 * This function assumes:
 *
 *      n > 2                   (n==2 has already been eliminated)
 *      h mod 2 == 1
 *      h < 2^n
 *      h*2^n-1 mod 3 != 0      (h*2^n-1 has no small factors, such as 3)
 *
 * The generation of v(1) depends on the value of h.  There are two cases
 * to consider, h mod 3 != 0, and h mod 3 == 0.
 *
 ***
 *
 * Case 1:      (h mod 3 != 0)
 *
 * This case is easy.
 *
 * In Ref1, page 869, one finds that if:        (or see Ref2, page 131-132)
 *
 *      h mod 6 == +/-1
 *      h*2^n-1 mod 3 != 0
 *
 * which translates, gives the functions assumptions, into the condition:
 *
 *      h mod 3 != 0
 *
 * If this case condition is true, then:
 *
 *      u(2) = (2+sqrt(3))^h + (2-sqrt(3))^h     (see Ref1, page 869)
 *           = (2+sqrt(3))^h + (2+sqrt(3))^(-h)  (NOTE: some call this u(2))
 *
 * and since Ref1, Theorem 5 states:
 *
 *      u(2) = alpha^h + alpha^(-h)              (NOTE: some call this u(2))
 *      r = abs(2^2 - 1^2*3) = 1
 *
 * where these values work for Case 1:           (h mod 3 != 0)
 *
 *      a = 1
 *      b = 2
 *      D = 1
 *
 * Now at the bottom of Ref1, page 872 states:
 *
 *      v(x) = alpha^x + alpha^(-x)
 *
 * If we let:
 *
 *      alpha = (2+sqrt(3))
 *
 * then
 *
 *      u(2) = v(h)                              (NOTE: some call this u(2))
 *
 * so we simply return
 *
 *      v(1) = alpha^1 + alpha^(-1)
 *           = (2+sqrt(3)) + (2-sqrt(3))
 *           *
 ***
 *
 * Case 2:      (h mod 3 == 0)
 *
 * For the case where h is a multiple of 3, we turn to Ref4.
 *
 * The central theorem on page 3 of that paper states that
 * we may set v(1) to the first value X that satisfies:
 *
 *      jacobi(X-2, h*2^n-1) == 1               (Ref4, condition 1)
 *      jacobi(X+2, h*2^n-1) == -1              (Ref4, condition 1)
 *
 *      NOTE: Ref4 uses P, which we shall refer to as X.
 *            Ref4 uses N, which we shall refer to as h*2^n-1.
 *
 *      NOTE: Ref4 uses the term Legendre-Jacobi symbol, which
 *            we shall refer to as the Jacobi symbol.
 *
 * Before we address the two conditions, we need some background information
 * on two symbols, Legendre and Jacobi.  In Ref 2, pp 278, 284-285, we find
 * the following definitions of jacobi(a,b) and L(a,p):
 *
 * The Legendre symbol L(a,p) takes the value:
 *
 *      L(a,p) == 1     => a is a quadratic residue of p
 *      L(a,p) == -1    => a is NOT a quadratic residue of p
 *
 * when:
 *
 *      p is prime
 *      p mod 2 == 1
 *      gcd(a,p) == 1
 *
 * The value a is a quadratic residue of b if there exists some integer z
 * such that:
 *
 *      z^2 mod b == a
 *
 * The Jacobi symbol jacobi(a,b) takes the value:
 *
 *      jacobi(a,b) == 1        => b is not prime,
 *                                 or a is a quadratic residue of b
 *      jacobi(a,b) == -1       => a is NOT a quadratic residue of b
 *
 * when
 *
 *      b mod 2 == 1
 *      gcd(a,b) == 1
 *
 * It is worth noting for the Legendre symbol, in order for L(X+/-2,
 * h*2^n-1) to be defined, we must ensure that neither X-2 nor X+2 are
 * factors of h*2^n-1.  This is done by pre-screening h*2^n-1 to not
 * have small factors and keeping X+2 less than that small factor
 * limit.  It is worth noting that in lucas(h, n), we first verify
 * that h*2^n-1 does not have a factor < 257 before performing the
 * Returning to the testing of conditions in Ref4, condition 1:
 *
 *      jacobi(X-2, h*2^n-1) == 1
 *      jacobi(X+2, h*2^n-1) == -1
 *
 * When such an X is found, we set:
 *
 *      v(1) = X
 *
 ***
 *
 * In conclusion, we can compute v,(1) by attempting to do the following:
 *
 * h mod 3 != 0
 *
 *     we return:
 *
 *         v(1) == 4
 *
 * h mod 3 == 0
 *
 *     we return:
 *
 *          v(1) = X
 *
 *     where X > 2 in a integer such that:
 *
 *          jacobi(X-2, h*2^n-1) == 1
 *          jacobi(X+2, h*2^n-1) == -1
 *
 ***
 *
 * given:
 *      h               h as in h*2^n-1 (h must be odd >= 1)
 *      n               n as in h*2^n-1 (must be >= 1)
 *      riesel_cand     pre-computed h*2^n-1 as an mpz_t
 *
 * returns:
 *      returns v(1)
 */
unsigned long
gen_v1(uint64_t h, uint64_t n, mpz_t riesel_cand)
{
    int x;			/* potential v(1) to test */
    int i;			/* x_tbl index */

    /*
     * check for Case 1:      (h mod 3 != 0)
     */
    if (h % 3 != 0) {

	/*
	 * v(1) is easy to compute
	 */
	return 4;
    }

    /*
     * special Mersenne number case: h == 1
     *
     * To match the historic Mersenne prime tests, we use v(1) == 4,
     * even though 40% of the time v(1) == 3 is allowed.  This lets us
     * match the results for those looking for Mersenne Primes (2^n-1).
     */
    if (h == 1) {

	/*
	 * v(1) is easy to compute for Mersenne number tests
	 */
	return 4;
    }

    /*
     * What follow is Case 2:      (h mod 3 == 0)
     */

    /*
     * We will look for x that satisfies conditions in Ref4, condition 1:
     *
     *      jacobi(X-2, h*2^n-1) == 1               part 1
     *      jacobi(X+2, h*2^n-1) == -1              part 2
     *
     * NOTE: If we wanted to be super optimial, we would cache
     *       jacobi(X+2, h*2^n-1) that that when we increment X
     *       to the next odd value, the now jacobi(X-2, h*2^n-1)
     *       does not need to be re-evaluted.
     */
    for (i = 0; i < X_TBL_LEN; ++i) {

	/*
	 * test Ref4 condition 1
	 */
	x = x_tbl[i];
	if (rodseth_xhn(x, riesel_cand) == 1) {

	    /*
	     * found a x that satisfies Ref4 condition 1
	     */
	    return x;
	}
    }

    /*
     * We are in that rare case (about 1 in 835 000) where none of the
     * common X values satisfy Ref4 condition 1.  We start a linear search
     * of odd vules at next_x from here on.
     */
    x = next_x;
    while (rodseth_xhn(x, riesel_cand) != 1) {
	x += 2;
    }

    /*
     * finally found a v(1) value beyond the end of the x_tbl[]
     */
    return x;
}


/*
 * rodseth_xhn - determine if v(1) == x for h*2^n-1
 *
 * For a given h*2^n-1, v(1) == x if:
 *
 *      jacobi(x-2, h*2^n-1) == 1               (Ref4, condition 1) part 1
 *      jacobi(x+2, h*2^n-1) == -1              (Ref4, condition 1) part 2
 *
 * Now when x-2 <= 0:
 *
 *      jacobi(x-2, h*2^n-1) == 0
 *
 * because:
 *
 *      jacobi(x,y) == 0                        if x <= 0
 *
 * So for (Ref4, condition 1) part 1 to be true:
 *
 *      x-2 > 0
 *
 * And therefore:
 *
 *      x > 2
 *
 * input:
 *      x       potential v(1) value
 *      riesel_cand     pre-computed h*2^n-1 as an mpz_t
 *
 * returns:
 *      1       if v(1) == x for h*2^n-1
 *      0       otherwise
 */
static int
rodseth_xhn(uint32_t x, mpz_t riesel_cand)
{
    mpz_t x_mp;

    /*
     * firewall
     */
    if (x <= 2) {
	return 0;
    }

    /*
     * Initialize X in GNUMP
     */
    mpz_init_set_ui(x_mp, x);

    /*
     * x = x - 2
     */
    mpz_sub_ui(x_mp, x_mp, 2ULL);

    /*
     * Check for jacobi(x-2, h*2^n-1) == 1  (Ref4, condition 1) part 1
     */
    if (mpz_jacobi(x_mp, riesel_cand) != 1) {
	return 0;
    }

    /*
     * x = x + 2
     */
    mpz_add_ui(x_mp, x_mp, 4ULL);

    /*
     * Check for jacobi(x+2, h*2^n-1) == -1 (Ref4, condition 1) part 2
     */
    if (mpz_jacobi(x_mp, riesel_cand) != -1) {
	return 0;
    }

    /*
     * v(1) == x for this h*2^n-1
     */
    mpz_clear(x_mp);
    return 1;
}
