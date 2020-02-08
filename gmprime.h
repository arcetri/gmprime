/*
 * gmprime - gmp h*2^n-1 Riesel primality tester
 *
 * Copyright (c) 2020 by Landon Curt Noll.  All Rights Reserved.
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


#if !defined(INCLUDE_GMPRIME_H)
#define INCLUDE_GMPRIME_H

/*
 * exit codes below 10 are reserved for non-critical errors
 *
 * IMPORTANT: If must change these defines, be sure to change the comments near where they are used
 * 	      as well as the usage string in gmprime.c.
 */
#define EXIT_IS_PRIME 0		// h*2^n-1 has been proven prime
#define EXIT_IS_COMPOSITE 1	// h*2^n-1 has been proven to be composite
/**/
#define EXIT_CANNOT_TEST 2	// h*2^n-1 is not a number for which the Riesel test applies (e.g., h > 2^n)
/**/
#define EXIT_3 3		// reserved for some test problem not related to an internal failure
/**/
#define EXIT_CHKPT_ACCESS 4	// checkpoint directory missing or not accessible
#define EXIT_LOCKED 5		// checkpoint directory locked by another process
#define EXIT_CANNOT_RESTORE 6	// cannot restore from checkpoint, checkpoint incomplete or malformed
#define EXIT_SIGNAL 7		// caught a signal, checkpointed and gracefully exited
#define EXIT_HELP 8		// help mode: print usage message and exit 8
#define EXIT_USAGE 9		// invalid, incompatible or missing flags and arguments

/*
 * internal error code ranges - numerical exit codes used
 */
/* NUMERIC EXIT CODES: 10-39	gmprime.c - reserved for internal errors */
/* NUMERIC EXIT CODES: 40-69	riesel.c - reserved for internal errors */
/* NUMERIC EXIT CODES: 70-99	checkpoint.c - reserved for internal errors */
/* NUMERIC EXIT CODES: 100-249	reserved for furure use */
/* NUMERIC EXIT CODES: 250-254	debug.c - reserved for internal errors */
/* NUMERIC EXIT CODES: 255	debug.c - FORCED_EXIT */

#endif /* INCLUDE_GMPRIME_H */
