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

#define _POSIX_SOURCE	/* for fileno() */
#define _BSD_SOURCE	/* for timerclear() */
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <gmp.h>

#include "debug.h"
#include "checkpt.h"

/*
 * prime test stats
 */
static struct prime_stats beginrun;		/* program start (or program restore) initial prime stats value */
static struct prime_stats teststart;		/* start of the primality test initial prime stats value */
static struct prime_stats current;		/* prime stats as of now */
static struct prime_stats lastckpt;		/* prime stats since program start or program restore */
static struct prime_stats total;		/* prime stats since start of the primality test */


/*
 * zerosize_stats - zeroize prime stats
 *
 * given:
 * 	ptr - pointer to a struct prime_stats or NULL
 *
 * NOTE: This function only warns if ptr is NULL and just returns.
 */
static void
zerosize_stats(struct prime_stats *ptr)
{
    /* paranoia */
    if (ptr == NULL) {
	warn(__func__, "ptr is NULL");
	return;
    }

    /* zeroize the prime stats */
    memset(ptr, 0, sizeof(struct prime_stats));
    timerclear(&ptr->now);		// clear the time now
    timerclear(&ptr->ru_utime);		// clear user CPU time used
    timerclear(&ptr->ru_stime);		// clear system CPU time used
    timerclear(&ptr->wall_clock);	// clear wall clock time uused
    return;
}


/*
 * load_prime_stats - load prime stats
 *
 * given:
 * 	ptr - pointer to a struct prime_stats or NULL
 *
 * NOTE: This function only warns if ptr is NULL and just returns.
 */
static void
load_prime_stats(struct prime_stats *ptr)
{
    struct rusage usage;	// our current resource usage

    /* paranoia */
    if (ptr == NULL) {
	warn(__func__, "ptr is NULL");
	return;
    }

    /* zerosize the stats */
    zerosize_stats(ptr);

    /* record the time */
    if (gettimeofday(&ptr->now, NULL) < 0) {
	warnp(__func__, "gettimeofday error");
    }

    /* get resource usage as of now */
    memset(&usage, 0, sizeof(usage));
    timerclear(&usage.ru_utime);
    timerclear(&usage.ru_stime);
    if (getrusage(RUSAGE_SELF, &usage) < 0) {
	warnp(__func__, "getrusage error");
    }

    /* load the relivant resource usage information into our prime stats */
    ptr->ru_utime = usage.ru_utime;
    ptr->ru_stime = usage.ru_stime;
    ptr->ru_maxrss = usage.ru_maxrss;
    ptr->ru_minflt = usage.ru_minflt;
    ptr->ru_majflt = usage.ru_majflt;
    ptr->ru_inblock = usage.ru_inblock;
    ptr->ru_oublock = usage.ru_oublock;
    ptr->ru_nvcsw = usage.ru_nvcsw;
    ptr->ru_nivcsw = usage.ru_nivcsw;

    /* prime stats has been loaded */
    return;
}


/*
 * initialize_beginrun_stats - setup prime stats for the start of this run
 */
void
initialize_beginrun_stats(void)
{
    /* initialize start of run prime stats */
    load_prime_stats(&beginrun);

    /* clear checkpoint prime stats for this run */
    zerosize_stats(&lastckpt);

    /* initialize checkpoint prime stats for this run */
    lastckpt.now = beginrun.now;
    lastckpt.ru_maxrss = beginrun.ru_maxrss;

    /* beginrun has been initialized */
    return;
}


/*
 * initialize_teststart_stats - setup prime stats for the start a primality test
 *
 * NOTE: This function also calls beginrun() so that both
 * 	 beginrun and teststart are initized to the same value.
 */
void
initialize_teststart_stats(void)
{
    /* initialize prime stats for the start of this run */
    initialize_beginrun_stats();

    /* clear teststart stats */
    zerosize_stats(&teststart);
    teststart.now = beginrun.now;
    teststart.ru_maxrss = beginrun.ru_maxrss;

    /* clear total */
    total = lastckpt;
    total.now = beginrun.now;
    total.ru_maxrss = beginrun.ru_maxrss;

    /* teststart and beginrun have been initialized */
    return;
}


/*
 * update_stats - update prime stats
 *
 * NOTE: This updates both the stats for this run and the stats
 * 	 for the entire primality test.
 */
void
update_stats(void)
{
    struct timeval diff;	// difference between now and start

    /* load prime stats for this checkpoint */
    load_prime_stats(&current);

    /*
     * update now
     */
    lastckpt.now = current.now;
    total.now = current.now;

    /*
     * update user CPU time used
     */
    if (!timercmp(&current.ru_utime, &beginrun.ru_utime, <)) {
	timersub(&current.ru_utime, &beginrun.ru_utime, &diff);
    } else {
	warn(__func__, "user CPU time went backwards, assuming 0 difference");
	timerclear(&diff);
    }
    lastckpt.ru_utime = diff;
    timeradd(&teststart.ru_utime, &diff, &total.ru_utime);

    /*
     * update system CPU time used
     */
    if (!timercmp(&current.ru_stime, &beginrun.ru_stime, <)) {
	timersub(&current.ru_stime, &beginrun.ru_stime, &diff);
    } else {
	warn(__func__, "user system CPU time went backwards, assuming 0 difference");
	timerclear(&diff);
    }
    lastckpt.ru_stime = diff;
    timeradd(&teststart.ru_stime, &diff, &total.ru_stime);

    /*
     * update wall clock time used
     */
    if (!timercmp(&current.now, &beginrun.now, <)) {
	timersub(&current.now, &beginrun.now, &diff);
    } else {
	warn(__func__, "user wall clock time went backwards, assuming 0 difference");
	timerclear(&diff);
    }
    lastckpt.wall_clock = diff;
    timeradd(&teststart.wall_clock, &diff, &total.wall_clock);

    /*
     * update maximum resident set size used in kilobytes
     */
    if (current.ru_maxrss > beginrun.ru_maxrss) {
	lastckpt.ru_maxrss = current.ru_maxrss;
    }
    if (current.ru_maxrss > teststart.ru_maxrss) {
	total.ru_maxrss = current.ru_maxrss;
    }

    /*
     * update page reclaims (soft page faults)
     */
    lastckpt.ru_minflt = current.ru_minflt - beginrun.ru_minflt;
    total.ru_minflt = teststart.ru_minflt + lastckpt.ru_minflt;

    /*
     * update page faults (hard page faults)
     */
    lastckpt.ru_majflt = current.ru_majflt - beginrun.ru_majflt;
    total.ru_majflt = teststart.ru_majflt + lastckpt.ru_majflt;

    /*
     * update block input operations
     */
    lastckpt.ru_inblock = current.ru_inblock - beginrun.ru_inblock;
    total.ru_inblock = teststart.ru_inblock + lastckpt.ru_inblock;

    /*
     * update block output operations
     */
    lastckpt.ru_oublock = current.ru_oublock - beginrun.ru_oublock;
    total.ru_oublock = teststart.ru_oublock + lastckpt.ru_oublock;

    /*
     * update voluntary context switches
     */
    lastckpt.ru_nvcsw = current.ru_nvcsw - beginrun.ru_nvcsw;
    total.ru_nvcsw = teststart.ru_nvcsw + lastckpt.ru_nvcsw;

    /*
     * update involuntary context switches
     */
    lastckpt.ru_nivcsw = current.ru_nivcsw - beginrun.ru_nivcsw;
    total.ru_nivcsw = teststart.ru_nivcsw + lastckpt.ru_nivcsw;

    /* stats and been updated */
    return;
}


/*
 * careful_write - carefully write to an open stream
 *
 * We will write to an open stream, taking care to detect
 * I/O errors, EOF and other problems.
 *
 * given:
 * 	calling_funcion_name - name of the calling function
 * 	    NOTE: usually passed as __func__
 *	stream - open checkpoint file stream to append to
 *	fmt - vfprintf format
 *
 * returns:
 * 	0 - no errors detected
 * 	!= 0 - return with this error code
 */
static int
careful_write(const char *calling_funcion_name, FILE *stream, char *fmt, ...)
{
    va_list ap;		/* argument pointer */
    int ret;		/* vfprintf() return value */

    /*
     * start the var arg setup and fetch our first arg
     */
    va_start(ap, fmt);

    /*
     * firewall
     */
    if (calling_funcion_name == NULL) {
	va_end(ap);	// clean up stdarg stuff
	warn(__func__, "calling_funcion_name is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (stream == NULL) {
	va_end(ap);	// clean up stdarg stuff
	warn(calling_funcion_name, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (fmt == NULL) {
	va_end(ap);	// clean up stdarg stuff
	warn(calling_funcion_name, "fmt is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (fileno(stream) < 0) {
	va_end(ap);	// clean up stdarg stuff
	warn(calling_funcion_name, "stream is not valid");
	return CHECKPT_INVALID_STREAM;
    }

    /*
     * clear errors and status prior to doing the write
     */
    clearerr(stream);
    errno = 0;

    /*
     * perform the write
     */
    ret = vfprintf(stream, fmt, ap);

    /*
     * analize the write
     */
    if (ret <= 0 || errno != 0 || ferror(stream) || feof(stream)) {
	if (errno == 0) {
	    if (feof(stream)) {
		warn(__func__, "EOF in careful_write called by %s, errno: 0, ret: %d",
		     calling_funcion_name, ret);
	    } else if (ferror(stream)) {
		warn(__func__, "ferror in careful_write called by %s, errno: 0, ret: %d",
		     calling_funcion_name, ret);
	    } else {
		warn(__func__, "error in careful_write called by %s, errno: 0, ret: %d",
		     calling_funcion_name, ret);
	    }
	    return CHECKPT_WRITE_ERRNO_ZERO_ERR;
	} else if (feof(stream)) {
	    warnp(__func__, "EOF in careful_write called by %s, errno: %d, ret: %d",
		  calling_funcion_name, errno, ret);
	    return errno;
	} else if (ferror(stream)) {
	    warnp(__func__, "ferror in careful_write called by %s, errno: %d, ret: %d",
		  calling_funcion_name, errno, ret);
	    return errno;
	} else {
	    warnp(__func__, "error in careful_write called by %s, errno: %d, ret: %d",
		  calling_funcion_name, errno, ret);
	    return errno;
	}
    }
    dbg(DBG_VVHIGH, "careful_write called by %s, vfprintf returned: %d",
     	calling_funcion_name, ret);

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_mpz_hex - write mpz value in hex to an open stream in calc format
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - name of variable to write
 *	value - const mpz_t value to write in hex
 *		NOTE: const __mpz_struct * is the same as const mpz_t
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
int
write_calc_mpz_hex(FILE *stream, char *name, const mpz_t value)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (name == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value == NULL) {
	warn(__func__, "value is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write hex variable prefix
     */
    ret = careful_write(__func__, stream, "%s = 0x", name);
    if (ret != 0) {
	warn(__func__, "write hex variable prefix: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * write value in hex
     *
     * NOTE: We have to duplicate some of the careful_write() logic
     * 	     because mpz_out_str returns 0 on I/O error.
     */
    clearerr(stream);
    errno = 0;
    ret = mpz_out_str(stream, 16, value);
    if (ret <= 0 || errno != 0 || ferror(stream) || feof(stream)) {
	if (errno == 0) {
	    if (feof(stream)) {
		warn(__func__, "EOF during mpz_out_str called from %s, errno: 0, ret: %d",
		     __func__, ret);
	    } else if (ferror(stream)) {
		warn(__func__, "ferror during mpz_out_str called from %s, errno: 0, ret: %d",
		     __func__, ret);
	    } else if (ret == 0) {
		warn(__func__, "zero return error in mpz_out_str called from %s, errno: 0, ret: %d",
		     __func__, ret);
	    } else {
		warn(__func__, "error during mpz_out_str called from %s, errno: 0, ret: %d",
		     __func__, ret);
	    }
	    return CHECKPT_MPZ_OUT_STR_ERR;
	} else if (feof(stream)) {
	    warnp(__func__, "EOF during mpz_out_str called from %s, errno: %d, ret: %d",
		  __func__, errno, ret);
	    return errno;
	} else if (ferror(stream)) {
	    warnp(__func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d",
		  __func__, errno, ret);
	    return errno;
	} else {
	    warnp(__func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d",
		  __func__, errno, ret);
	    return errno;
	}
    }

    /*
     * write hex variable suffix
     */
    ret = careful_write(__func__, stream, " ;\n");
    if (ret != 0) {
	warn(__func__, "write hex variable suffix: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_int64_t - write int64_t value to an open stream in calc format
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - name of variable to write
 *	value - int64_t value to write
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
int
write_calc_int64_t(FILE *stream, char *name, const int64_t value)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (name == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    ret = careful_write(__func__, stream, "%s = %" PRId64 " ;\n", name, value);
    if (ret != 0) {
	warn(__func__, "write int64_t: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_int64_t2 - write int64_t value to an open stream in calc format with a 2 part name
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - name of variable to write
 *	value - int64_t value to write
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
static int
write_calc_int64_t2(FILE *stream, char *basename, char *subname, const int64_t value)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (basename == NULL) {
	warn(__func__, "basename is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    ret = careful_write(__func__, stream, "%s_%s = %" PRId64 " ;\n", basename, subname, value);
    if (ret != 0) {
	warn(__func__, "write int64_t: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_uint64_t - write uint64_t value to an open stream in calc format
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - name of variable to write
 *	value - uint64_t value to write
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
int
write_calc_uint64_t(FILE *stream, char *name, const uint64_t value)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (name == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    ret = careful_write(__func__, stream, "%s = %" PRIu64 " ;\n", name, value);
    if (ret != 0) {
	warn(__func__, "write uint64_t: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_str - write string to an open stream in calc format
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - name of variable to write
 *	value - string to write
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
int
write_calc_str(FILE *stream, char *name, const char *value)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (name == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    ret = careful_write(__func__, stream, "%s = \"%s\" ;\n", name, value);
    if (ret != 0) {
	warn(__func__, "write string: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_str2 - write string to an open stream in calc format with a 2 part name
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - name of variable to write
 *	value - string to write
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
static int
write_calc_str2(FILE *stream, char *basename, char *subname, const char *value)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (basename == NULL) {
	warn(__func__, "basename is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    ret = careful_write(__func__, stream, "%s_%s = \"%s\" ;\n", basename, subname, value);
    if (ret != 0) {
	warn(__func__, "write string: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_timeval - write a time value in seconds.microseconds to an open stream in calc format
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - variable name
 *	value_ptr - pointer to a struct timeval
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
int
write_calc_timeval(FILE *stream, char *name, const struct timeval *value_ptr)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (name == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value_ptr == NULL) {
	warn(__func__, "value_ptr is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    ret = careful_write(__func__, stream, "%s = %" PRIu64 ".%06d ;\n", name, value_ptr->tv_sec, value_ptr->tv_usec);
    if (ret != 0) {
	warn(__func__, "write timeval: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_timeval2 - write a time value in seconds.microseconds to an open stream in calc format with a 2 part name
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	basename - base variable name
 *	subname - subcomponent variable name
 *	value_ptr - pointer to a struct timeval
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
static int
write_calc_timeval2(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr)
{
    int ret;		/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (basename == NULL) {
	warn(__func__, "basename is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value_ptr == NULL) {
	warn(__func__, "value_ptr is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    ret = careful_write(__func__, stream, "%s_%s = %" PRIu64 ".%06d ;\n", basename, subname, value_ptr->tv_sec, value_ptr->tv_usec);
    if (ret != 0) {
	warn(__func__, "write timeval: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_date_time_str - write a time value in date time string to an open stream in calc format
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	name - variable name
 *	value_ptr - pointer to a struct timeval
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
int
write_calc_date_time_str(FILE *stream, char *name, const struct timeval *value_ptr)
{
    int ret;		/* careful_write return value */
    struct tm *tm_time;		/* broken-down time */
    char buf[BUFSIZ+1];		/* time string buffer */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (name == NULL) {
	warn(__func__, "name is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value_ptr == NULL) {
	warn(__func__, "value_ptr is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * convert timeval to broken-down time
     */
    tm_time = gmtime(&value_ptr->tv_sec);
    if (tm_time == NULL) {
	if (errno == 0) {
	    warn(__func__, "gmtime returned NULL, errno = 0");
	    return CHECKPT_GMTIIME_ERR;
	} else {
	    warnp(__func__, "gmtime returned NULL, errno = %d", errno);
	    return errno;
	}
    }

    /*
     * format as a date and time string
     */
    ret = strftime(buf, BUFSIZ, "%F %T UTC", tm_time);
    if (ret <= 0) {
	if (errno == 0) {
	    warnp(__func__, "strftime returned %d, errno = 0", ret);
	    return CHECKPT_STRFTIME_ERR;
	} else {
	    warnp(__func__, "strftime returned %d, errno = %d", ret, errno);
	    return errno;
	}
    }
    buf[BUFSIZ] = '\0';	// paranoia

    /*
     * write date and time as a calc string
     */
    ret = write_calc_str(stream, name, buf);
    if (ret != 0) {
	warn(__func__, "write write_calc_str careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_date_time_str2 - write a time value in date time string to an open stream in calc format with a 2 part name
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	basename - base variable name
 *	subname - subcomponent variable name
 *	value_ptr - pointer to a struct timeval
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
static int
write_calc_date_time_str2(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr)
{
    int ret;		/* careful_write return value */
    struct tm *tm_time;		/* broken-down time */
    char buf[BUFSIZ+1];		/* time string buffer */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (basename == NULL) {
	warn(__func__, "basename is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value_ptr == NULL) {
	warn(__func__, "value_ptr is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * convert timeval to broken-down time
     */
    tm_time = gmtime(&value_ptr->tv_sec);
    if (tm_time == NULL) {
	if (errno == 0) {
	    warn(__func__, "gmtime returned NULL, errno = 0");
	    return CHECKPT_GMTIIME_ERR;
	} else {
	    warnp(__func__, "gmtime returned NULL, errno = %d", errno);
	    return errno;
	}
    }

    /*
     * format as a date and time string
     */
    ret = strftime(buf, BUFSIZ, "%F %T UTC", tm_time);
    if (ret <= 0) {
	if (errno == 0) {
	    warnp(__func__, "strftime returned %d, errno = 0", ret);
	    return CHECKPT_STRFTIME_ERR;
	} else {
	    warnp(__func__, "strftime returned %d, errno = %d", ret, errno);
	    return errno;
	}
    }
    buf[BUFSIZ] = '\0';	// paranoia

    /*
     * write date and time as a calc string
     */
    ret = write_calc_str2(stream, basename, subname, buf);
    if (ret != 0) {
	warn(__func__, "write write_calc_str careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_prime_stats_ptr - write prime stats to an open stream in calc format using a basename
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *	basename - base variable name
 *	value_ptr - pointer to a struct prime_stats
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
static int
write_calc_prime_stats_ptr(FILE *stream, char *basename, struct prime_stats *ptr)
{
    int ret;			/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (basename == NULL) {
	warn(__func__, "basename is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (ptr == NULL) {
	warn(__func__, "ptr is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write start time as a timestamp since the epoch
     */
    ret = write_calc_timeval2(stream, basename, "timestamp", &ptr->now);
    if (ret != 0) {
	warn(__func__, "write %s_timestamp: write_calc_timeval2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write start time as a timestamp since as a string
     */
    ret = write_calc_date_time_str2(stream, basename, "date_time", &ptr->now);
    if (ret != 0) {
	warn(__func__, "write %s_date_time write_calc_date_time_str2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write user CPU time used
     */
    ret = write_calc_timeval2(stream, basename, "ru_utime", &ptr->ru_utime);
    if (ret != 0) {
	warn(__func__, "write %s_ru_utime: write_calc_timeval2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write system CPU time used
     */
    ret = write_calc_timeval2(stream, basename, "ru_stime", &ptr->ru_stime);
    if (ret != 0) {
	warn(__func__, "write %s_ru_stime: write_calc_timeval2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write wall clock time used
     */
    ret = write_calc_timeval2(stream, basename, "wall_clock", &ptr->wall_clock);
    if (ret != 0) {
	warn(__func__, "write %s_wall_clock: write_calc_timeval2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write maximum resident set size used in kilobytes
     */
    ret = write_calc_int64_t2(stream, basename, "ru_maxrss", ptr->ru_maxrss);
    if (ret != 0) {
	warn(__func__, "write %s_ru_maxrss: write_calc_int64_t2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write page reclaims (soft page faults)
     */
    ret = write_calc_int64_t2(stream, basename, "ru_minflt", ptr->ru_minflt);
    if (ret != 0) {
	warn(__func__, "write %s_ru_minflt: write_calc_int64_t2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write page faults (hard page faults)
     */
    ret = write_calc_int64_t2(stream, basename, "ru_majflt", ptr->ru_majflt);
    if (ret != 0) {
	warn(__func__, "write %s_ru_majflt: write_calc_int64_t2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write block input operations
     */
    ret = write_calc_int64_t2(stream, basename, "ru_inblock", ptr->ru_inblock);
    if (ret != 0) {
	warn(__func__, "write %s_ru_inblock: write_calc_int64_t2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write block output operations
     */
    ret = write_calc_int64_t2(stream, basename, "ru_oublock", ptr->ru_oublock);
    if (ret != 0) {
	warn(__func__, "write %s_ru_oublock: write_calc_int64_t2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write voluntary context switches
     */
    ret = write_calc_int64_t2(stream, basename, "ru_nvcsw", ptr->ru_nvcsw);
    if (ret != 0) {
	warn(__func__, "write %s_ru_nvcsw: write_calc_int64_t2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write involuntary context switches
     */
    ret = write_calc_int64_t2(stream, basename, "ru_nivcsw", ptr->ru_nivcsw);
    if (ret != 0) {
	warn(__func__, "write %s_ru_nivcsw: write_calc_int64_t2 return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_prime_stats - write prime stats to an open stream in calc format using a basename
 *
 * given:
 *	stream - open checkpoint file stream to append to
 *
 * returns:
 * 	0 - no errors detected
 * 	< 0 - internal error
 * 	> 0 - errno error value
 */
int
write_calc_prime_stats(FILE *stream)
{
    int ret;			/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write beginrun stats
     */
    ret = write_calc_prime_stats_ptr(stream, "beginrun", &beginrun);
    if (ret != 0) {
	warn(__func__, "write beginrun: write_calc_prime_stats_ptr return: %d != 0", ret);
	return ret;
    }

    /*
     * write teststart stats
     */
    ret = write_calc_prime_stats_ptr(stream, "teststart", &teststart);
    if (ret != 0) {
	warn(__func__, "write teststart: write_calc_prime_stats_ptr return: %d != 0", ret);
	return ret;
    }

    /*
     * write current stats
     */
    ret = write_calc_prime_stats_ptr(stream, "current", &current);
    if (ret != 0) {
	warn(__func__, "write current: write_calc_prime_stats_ptr return: %d != 0", ret);
	return ret;
    }

    /*
     * write lastckpt stats
     */
    ret = write_calc_prime_stats_ptr(stream, "lastckpt", &lastckpt);
    if (ret != 0) {
	warn(__func__, "write lastckpt: write_calc_prime_stats_ptr return: %d != 0", ret);
	return ret;
    }

    /*
     * write total stats
     */
    ret = write_calc_prime_stats_ptr(stream, "total", &total);
    if (ret != 0) {
	warn(__func__, "write total: write_calc_prime_stats_ptr return: %d != 0", ret);
	return ret;
    }

    /*
     * no errors detected
     */
    return 0;
}
