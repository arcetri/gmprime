/*
 *
 * 			Credits: Konstantin Simeonov
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
uint8_t
gen_u0(uint64_t h, uint64_t n, mpz_t u2);

#endif /*GEN_U2_LIBRARY_H*/
