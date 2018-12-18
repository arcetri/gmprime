/*
 * checkpt - checkpoint and restore stilities
 *
 * Copyright (c) 2018 by Landon Curt Noll.  All Rights Reserved.
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


#if !defined(INCLUDE_CHECKPT_H)
#define INCLUDE_CHECKPT_H

#include <stdint.h>
#include <sys/time.h>


/* checkpoint fornmat version */
#define CHECKPT_FMT_VERSION		(2)	// current version of checkpoint files


/* internal error codes */
#define CHECKPT_NULL_PTR		(-1)	// NULL point argument found
#define CHECKPT_WRITE_ERRNO_ZERO_ERR	(-2)	// write() error with zero errno
#define CHECKPT_INVALID_STREAM		(-3)	// stream arg is not a valid stream
#define CHECKPT_MPZ_OUT_STR_ERR		(-4)	// mpz_out_str error
#define CHECKPT_GMTIIME_ERR		(-5)	// gmtime() error with zero errno
#define CHECKPT_STRFTIME_ERR		(-6)	// strftime() error with zero errno


/*
 * primality test stats
 */
struct prime_stats {
    struct timeval now;		/* start time or the current time */
    struct timeval ru_utime;	/* user CPU time used */
    struct timeval ru_stime;	/* system CPU time used */
    struct timeval wall_clock;	/* wall clock time used */
    long ru_maxrss;		/* maximum resident set size used in kilobytes */
    long ru_minflt;		/* page reclaims (soft page faults) */
    long ru_majflt;		/* page faults (hard page faults) */
    long ru_inblock;		/* block input operations */
    long ru_oublock;		/* block output operations */
    long ru_nvcsw;		/* voluntary context switches */
    long ru_nivcsw;		/* involuntary context switches */
};


/* extern functions */
extern void initialize_beginrun_stats(void);
extern void initialize_teststart_stats(void);
extern void update_stats(void);
extern int write_calc_mpz_hex(FILE *stream, char *name, const mpz_t value);
extern int write_calc_int64_t(FILE *stream, char *name, const int64_t value);
extern int write_calc_uint64_t(FILE *stream, char *name, const uint64_t value);
extern int write_calc_str(FILE *stream, char *name, const char *value);
extern int write_calc_timeval(FILE *stream, char *name, const struct timeval *value_ptr);
extern int write_calc_date_time_str(FILE *stream, char *name, const struct timeval *value_ptr);
extern int write_calc_prime_stats(FILE *stream);

#endif /* !INCLUDE_CHECKPT_H */
