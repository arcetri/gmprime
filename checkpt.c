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

#define _POSIX_SOURCE		/* for fileno() */
#define _BSD_SOURCE		/* for timerclear() */
#if defined(__APPLE__)
#    define _DARWIN_C_SOURCE	/* for macOS */
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <gmp.h>

#include "debug.h"
#include "checkpt.h"

/*
 * checkpoint flags
 */
static uint64_t checkpoint_alarm = 0;	/* != 0 ==> a SIGALRM or SIGVTALRM went off, checkpoint and continue */
static uint64_t checkpoint_and_end = 0;	/* != 0 ==> a SIGINT went off, checkpoint and exit */

/*
 * prime test stats
 */
static struct prime_stats beginrun;	/* program start (or program restore) prime stats */
static struct prime_stats current;	/* prime stats as of now */
static struct prime_stats restored;	/* overall prime stats since restore (or start of not prior restore) */
static struct prime_stats total;	/* updated total prime stats since start of the primality test */

/*
 * static functions
 */
static void record_sigalarm(int signum);
static void record_sigint(int signum);
static void zerosize_stats(struct prime_stats *ptr);
static void load_prime_stats(struct prime_stats *ptr);
static int careful_write(const char *calling_funcion_name, FILE * stream, char *fmt, ...);
static int write_calc_timeval(FILE * stream, char *basename, char *subname, const struct timeval *value_ptr);
static int write_calc_date_time_str(FILE * stream, char *basename, char *subname, const struct timeval *value_ptr);
static int write_calc_prime_stats_ptr(FILE * stream, char *basename, struct prime_stats *ptr);
static int mkdirp(char *path_arg, int mode, int duplicate);


/*
 * record_sigalarm - record a SIGALRM
 *
 * given:
 *      signum          the signal that has been delivered
 *
 * This is the signal handler for the SIGALRM or SIGVTALRM.
 * It sets the checkpoint_alarm value to 1 and returns.
 *
 * Another function at a time may consult the checkpoint_alarm
 * value and perform the checkpoint as needed.
 */
static void
record_sigalarm(int signum)
{
    /*
     * note when non-SIGALRM and non-SIGVTALRM is received
     */
    if (signum != SIGALRM && signum != SIGVTALRM) {
	fflush(stdout);
	warn(__func__, "non-SIGALRM and non-SIGVTALRM detected: %d", signum);
	fflush(stderr);
    }

    /*
     * checkpoint at next opportunity
     */
    if (checkpoint_alarm) {
	warn(__func__, "previous checkpoint_alarm value not cleared: %d", checkpoint_and_end);
    }
    ++checkpoint_alarm;
    if (checkpoint_alarm <= 0) {
	warn(__func__, "checkpoint_alarm counter wraparound, reset to 1");
	checkpoint_alarm = 1;
    }
    return;
}


/*
 * record_sigint - record a SIGINT
 *
 * given:
 *      signum          the signal that has been delivered
 *
 * This is the signal handler for the SIGINT.
 * It sets the checkpoint_alarm value to 1 and returns.
 *
 * Another function at a time may consult the checkpoint_alarm
 * value and perform the checkpoint as needed and then exit.
 * This function does not exit. It is the responbility of
 * the appropriate checkpoint function to exit.
 */
static void
record_sigint(int signum)
{
    /*
     * note when non-SIGALRM is received
     */
    if (signum != SIGINT) {
	fflush(stdout);
	warn(__func__, "non-SIGINT detected: %d", signum);
	fflush(stderr);
    }

    /*
     * checkpoint at next opportunity and then exit
     */
    if (checkpoint_and_end) {
	warn(__func__, "previous checkpoint_and_end value not cleared: %d", checkpoint_and_end);
    }
    ++checkpoint_and_end;
    if (checkpoint_and_end <= 0) {
	warn(__func__, "checkpoint_and_end counter wraparound, reset to 1");
	checkpoint_and_end = 1;
    }
    return;
}


/*
 * zerosize_stats - zeroize prime stats
 *
 * given:
 *      ptr - pointer to a struct prime_stats or NULL
 *
 * NOTE: This function only warns if ptr is NULL and just returns.
 */
static void
zerosize_stats(struct prime_stats *ptr)
{
    /*
     * paranoia
     */
    if (ptr == NULL) {
	warn(__func__, "ptr is NULL");
	return;
    }

    /*
     * zeroize the prime stats
     */
    memset(ptr, 0, sizeof(struct prime_stats));
    timerclear(&ptr->now);	// clear the time now
    timerclear(&ptr->ru_utime);	// clear user CPU time used
    timerclear(&ptr->ru_stime);	// clear system CPU time used
    timerclear(&ptr->wall_clock);	// clear wall clock time uused
    return;
}


/*
 * load_prime_stats - load prime stats
 *
 * given:
 *      ptr - pointer to a struct prime_stats or NULL
 *
 * NOTE: This function only warns if ptr is NULL and just returns.
 */
static void
load_prime_stats(struct prime_stats *ptr)
{
    struct rusage usage;	// our current resource usage

    /*
     * paranoia
     */
    if (ptr == NULL) {
	warn(__func__, "ptr is NULL");
	return;
    }

    /*
     * zerosize the stats
     */
    zerosize_stats(ptr);

    /*
     * record the time
     */
    if (gettimeofday(&ptr->now, NULL) < 0) {
	warnp(__func__, "gettimeofday error");
    }

    /*
     * get resource usage as of now
     */
    memset(&usage, 0, sizeof(usage));
    timerclear(&usage.ru_utime);
    timerclear(&usage.ru_stime);
    if (getrusage(RUSAGE_SELF, &usage) < 0) {
	warnp(__func__, "getrusage error");
    }

    /*
     * load the relivant resource usage information into our prime stats
     */
    ptr->ru_utime = usage.ru_utime;
    ptr->ru_stime = usage.ru_stime;
    ptr->ru_maxrss = usage.ru_maxrss;
    ptr->ru_minflt = usage.ru_minflt;
    ptr->ru_majflt = usage.ru_majflt;
    ptr->ru_inblock = usage.ru_inblock;
    ptr->ru_oublock = usage.ru_oublock;
    ptr->ru_nvcsw = usage.ru_nvcsw;
    ptr->ru_nivcsw = usage.ru_nivcsw;

    /*
     * prime stats has been loaded
     */
    return;
}


/*
 * careful_write - carefully write to an open stream
 *
 * We will write to an open stream, taking care to detect
 * I/O errors, EOF and other problems.
 *
 * given:
 *      calling_funcion_name - name of the calling function
 *          NOTE: usually passed as __func__
 *      stream - open checkpoint file stream to append to
 *      fmt - vfprintf format
 *
 * returns:
 *      0 - no errors detected
 *      != 0 - return with this error code
 */
static int
careful_write(const char *calling_funcion_name, FILE * stream, char *fmt, ...)
{
    va_list ap;			/* argument pointer */
    int ret;			/* vfprintf() return value */

    /*
     * start the var arg setup and fetch our first arg
     */
    va_start(ap, fmt);

    /*
     * firewall
     */
    if (calling_funcion_name == NULL) {
	va_end(ap);		// clean up stdarg stuff
	warn(__func__, "calling_funcion_name is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (stream == NULL) {
	va_end(ap);		// clean up stdarg stuff
	warn(calling_funcion_name, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (fmt == NULL) {
	va_end(ap);		// clean up stdarg stuff
	warn(calling_funcion_name, "fmt is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (fileno(stream) < 0) {
	va_end(ap);		// clean up stdarg stuff
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
		warn(__func__, "EOF in careful_write called by %s, errno: 0, ret: %d", calling_funcion_name, ret);
	    } else if (ferror(stream)) {
		warn(__func__, "ferror in careful_write called by %s, errno: 0, ret: %d", calling_funcion_name, ret);
	    } else {
		warn(__func__, "error in careful_write called by %s, errno: 0, ret: %d", calling_funcion_name, ret);
	    }
	    return CHECKPT_WRITE_ERRNO_ZERO_ERR;
	} else if (feof(stream)) {
	    warnp(__func__, "EOF in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	    return errno;
	} else if (ferror(stream)) {
	    warnp(__func__, "ferror in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	    return errno;
	} else {
	    warnp(__func__, "error in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	    return errno;
	}
    }
    dbg(DBG_VVHIGH, "careful_write called by %s, vfprintf returned: %d", calling_funcion_name, ret);

    /*
     * no errors detected
     */
    return 0;
}


/*
 * write_calc_mpz_hex - write mpz value in hex to an open stream in calc format
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value - const mpz_t value to write in hex
 *              NOTE: const __mpz_struct * is the same as const mpz_t
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
int
write_calc_mpz_hex(FILE * stream, char *basename, char *subname, const mpz_t value)
{
    int ret;			/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value == NULL) {
	warn(__func__, "value is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write hex variable prefix
     */
    if (basename == NULL) {
	ret = careful_write(__func__, stream, "%s = 0x", subname);
    } else {
	ret = careful_write(__func__, stream, "%s_%s = 0x", basename, subname);
    }
    if (ret != 0) {
	warn(__func__, "write hex variable prefix: careful_write return: %d != 0", ret);
	return ret;
    }

    /*
     * write value in hex
     *
     * NOTE: We have to duplicate some of the careful_write() logic
     *       because mpz_out_str returns 0 on I/O error.
     */
    clearerr(stream);
    errno = 0;
    ret = mpz_out_str(stream, 16, value);
    if (ret <= 0 || errno != 0 || ferror(stream) || feof(stream)) {
	if (errno == 0) {
	    if (feof(stream)) {
		warn(__func__, "EOF during mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    } else if (ferror(stream)) {
		warn(__func__, "ferror during mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    } else if (ret == 0) {
		warn(__func__, "zero return error in mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    } else {
		warn(__func__, "error during mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    }
	    return CHECKPT_MPZ_OUT_STR_ERR;
	} else if (feof(stream)) {
	    warnp(__func__, "EOF during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
	    return errno;
	} else if (ferror(stream)) {
	    warnp(__func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
	    return errno;
	} else {
	    warnp(__func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
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
 * write_calc_int64_t - write int64_t value to an open stream in calc format with a 2 part name
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value - int64_t value to write
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
int
write_calc_int64_t(FILE * stream, char *basename, char *subname, const int64_t value)
{
    int ret;			/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	ret = careful_write(__func__, stream, "%s = %" PRId64 " ;\n", subname, value);
    } else {
	ret = careful_write(__func__, stream, "%s_%s = %" PRId64 " ;\n", basename, subname, value);
    }
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
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value - uint64_t value to write
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
int
write_calc_uint64_t(FILE * stream, char *basename, char *subname, const uint64_t value)
{
    int ret;			/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	ret = careful_write(__func__, stream, "%s_%s = %" PRIu64 " ;\n", basename, subname, value);
    } else {
	ret = careful_write(__func__, stream, "%s = %" PRIu64 " ;\n", subname, value);
    }
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
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value - string to write
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
int
write_calc_str(FILE * stream, char *basename, char *subname, const char *value)
{
    int ret;			/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (subname == NULL) {
	warn(__func__, "subname is NULL");
	return CHECKPT_NULL_PTR;
    }
    if (value == NULL) {
	warn(__func__, "value is NULL");
	return CHECKPT_NULL_PTR;
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	ret = careful_write(__func__, stream, "%s = \"%s\" ;\n", subname, value);
    } else {
	ret = careful_write(__func__, stream, "%s_%s = \"%s\" ;\n", basename, subname, value);
    }
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
 * write_calc_timeval - write a time value in seconds.microseconds to an open stream in calc format with a 2 part name
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value_ptr - pointer to a struct timeval
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
static int
write_calc_timeval(FILE * stream, char *basename, char *subname, const struct timeval *value_ptr)
{
    int ret;			/* careful_write return value */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
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
    if (basename == NULL) {
	ret = careful_write(__func__, stream, "%s = %" PRIu64 ".%06d ;\n", subname, value_ptr->tv_sec, value_ptr->tv_usec);
    } else {
	ret = careful_write(__func__, stream, "%s_%s = %" PRIu64 ".%06d ;\n",
			    basename, subname, value_ptr->tv_sec, value_ptr->tv_usec);
    }
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
 * write_calc_date_time_str - write a time value in date time string to an open stream in calc format with a 2 part name
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value_ptr - pointer to a struct timeval
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
static int
write_calc_date_time_str(FILE * stream, char *basename, char *subname, const struct timeval *value_ptr)
{
    int ret;			/* careful_write return value */
    struct tm *tm_time;		/* broken-down time */
    char buf[BUFSIZ + 1];	/* time string buffer */

    /*
     * firewall
     */
    if (stream == NULL) {
	warn(__func__, "stream is NULL");
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
	    warn(__func__, "gmtime returned NULL, errno: 0");
	    return CHECKPT_GMTIIME_ERR;
	} else {
	    warnp(__func__, "gmtime returned NULL, errno: %d", errno);
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
    buf[BUFSIZ] = '\0';		// paranoia

    /*
     * write date and time as a calc string
     */
    ret = write_calc_str(stream, basename, subname, buf);
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
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name
 *      value_ptr - pointer to a struct prime_stats
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
static int
write_calc_prime_stats_ptr(FILE * stream, char *basename, struct prime_stats *ptr)
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
    ret = write_calc_timeval(stream, basename, "timestamp", &ptr->now);
    if (ret != 0) {
	warn(__func__, "write %s_timestamp: write_calc_timeval return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write start time as a timestamp since as a string
     */
    ret = write_calc_date_time_str(stream, basename, "date_time", &ptr->now);
    if (ret != 0) {
	warn(__func__, "write %s_date_time write_calc_date_time_str return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write user CPU time used
     */
    ret = write_calc_timeval(stream, basename, "ru_utime", &ptr->ru_utime);
    if (ret != 0) {
	warn(__func__, "write %s_ru_utime: write_calc_timeval return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write system CPU time used
     */
    ret = write_calc_timeval(stream, basename, "ru_stime", &ptr->ru_stime);
    if (ret != 0) {
	warn(__func__, "write %s_ru_stime: write_calc_timeval return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write wall clock time used
     */
    ret = write_calc_timeval(stream, basename, "wall_clock", &ptr->wall_clock);
    if (ret != 0) {
	warn(__func__, "write %s_wall_clock: write_calc_timeval return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write maximum resident set size used in kilobytes
     */
    ret = write_calc_int64_t(stream, basename, "ru_maxrss", ptr->ru_maxrss);
    if (ret != 0) {
	warn(__func__, "write %s_ru_maxrss: write_calc_int64_t return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write page reclaims (soft page faults)
     */
    ret = write_calc_int64_t(stream, basename, "ru_minflt", ptr->ru_minflt);
    if (ret != 0) {
	warn(__func__, "write %s_ru_minflt: write_calc_int64_t return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write page faults (hard page faults)
     */
    ret = write_calc_int64_t(stream, basename, "ru_majflt", ptr->ru_majflt);
    if (ret != 0) {
	warn(__func__, "write %s_ru_majflt: write_calc_int64_t return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write block input operations
     */
    ret = write_calc_int64_t(stream, basename, "ru_inblock", ptr->ru_inblock);
    if (ret != 0) {
	warn(__func__, "write %s_ru_inblock: write_calc_int64_t return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write block output operations
     */
    ret = write_calc_int64_t(stream, basename, "ru_oublock", ptr->ru_oublock);
    if (ret != 0) {
	warn(__func__, "write %s_ru_oublock: write_calc_int64_t return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write voluntary context switches
     */
    ret = write_calc_int64_t(stream, basename, "ru_nvcsw", ptr->ru_nvcsw);
    if (ret != 0) {
	warn(__func__, "write %s_ru_nvcsw: write_calc_int64_t return: %d != 0", basename, ret);
	return ret;
    }

    /*
     * write involuntary context switches
     */
    ret = write_calc_int64_t(stream, basename, "ru_nivcsw", ptr->ru_nivcsw);
    if (ret != 0) {
	warn(__func__, "write %s_ru_nivcsw: write_calc_int64_t return: %d != 0", basename, ret);
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
 *      stream - open checkpoint file stream to append to
 *      extended - 0 ==> write only total stats, != 0 ==> write all stats
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
int
write_calc_prime_stats(FILE * stream, int extended)
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
     * if -t was given
     */
    if (extended) {

	/*
	 * write beginrun stats
	 */
	ret = write_calc_prime_stats_ptr(stream, "beginrun", &beginrun);
	if (ret != 0) {
	    warn(__func__, "write beginrun: write_calc_prime_stats_ptr return: %d != 0", ret);
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
	 * write restored stats
	 */
	ret = write_calc_prime_stats_ptr(stream, "restored", &restored);
	if (ret != 0) {
	    warn(__func__, "write restored: write_calc_prime_stats_ptr return: %d != 0", ret);
	    return ret;
	}
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


/*
 * initialize_beginrun_stats - setup prime stats for the start of this run
 */
void
initialize_beginrun_stats(void)
{
    /*
     * initialize start of run prime stats
     */
    load_prime_stats(&beginrun);

    /*
     * beginrun has been initialized
     */
    return;
}


/*
 * initialize_total_stats - setup prime stats for the start a primality test
 *
 * NOTE: This function also calls initialize_beginrun_stats() so that both
 *       beginrun and total are initized to the same value.
 */
void
initialize_total_stats(void)
{
    /*
     * initialize prime stats for the start of this run
     */
    initialize_beginrun_stats();

    /*
     * no prior restore so clear restored stats
     */
    zerosize_stats(&restored);
    restored.now = beginrun.now;
    restored.ru_maxrss = beginrun.ru_maxrss;

    /*
     * total will be the same as cleared restore stats
     */
    total = restored;

    /*
     * beginrun and total have been initialized
     */
    return;
}


/*
 * update_stats - update prime stats
 *
 * NOTE: This updates both the stats for this run and the stats
 *       for the entire primality test.
 */
void
update_stats(void)
{
    struct timeval diff;	// difference between now and start

    /*
     * load prime stats for this checkpoint
     */
    load_prime_stats(&current);

    /*
     * update now
     */
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
    timeradd(&restored.ru_utime, &diff, &total.ru_utime);

    /*
     * update system CPU time used
     */
    if (!timercmp(&current.ru_stime, &beginrun.ru_stime, <)) {
	timersub(&current.ru_stime, &beginrun.ru_stime, &diff);
    } else {
	warn(__func__, "user system CPU time went backwards, assuming 0 difference");
	timerclear(&diff);
    }
    timeradd(&restored.ru_stime, &diff, &total.ru_stime);

    /*
     * update wall clock time used
     */
    if (!timercmp(&current.now, &beginrun.now, <)) {
	timersub(&current.now, &beginrun.now, &diff);
    } else {
	warn(__func__, "user wall clock time went backwards, assuming 0 difference");
	timerclear(&diff);
    }
    current.wall_clock = diff;
    timeradd(&restored.wall_clock, &diff, &total.wall_clock);

    /*
     * update maximum resident set size used in kilobytes
     */
    if (current.ru_maxrss > total.ru_maxrss) {
	total.ru_maxrss = current.ru_maxrss;
    }

    /*
     * update page reclaims (soft page faults)
     */
    total.ru_minflt = current.ru_minflt - beginrun.ru_minflt + restored.ru_minflt;

    /*
     * update page faults (hard page faults)
     */
    total.ru_majflt = current.ru_majflt - beginrun.ru_majflt + restored.ru_majflt;

    /*
     * update block input operations
     */
    total.ru_inblock = current.ru_inblock - beginrun.ru_inblock + restored.ru_inblock;

    /*
     * update block output operations
     */
    total.ru_oublock = current.ru_oublock - beginrun.ru_oublock + restored.ru_oublock;

    /*
     * update voluntary context switches
     */
    total.ru_nvcsw = current.ru_nvcsw - beginrun.ru_nvcsw + restored.ru_nvcsw;

    /*
     * update involuntary context switches
     */
    total.ru_nivcsw = current.ru_nivcsw - beginrun.ru_nivcsw + restored.ru_nivcsw;

    /*
     * stats and been updated
     */
    return;
}


/*
 * mkdirp - create path
 *
 * given:
 *      path_arg        path to create
 *      mode            mode to create directories
 *      duplicate       0 ==> do not pre-duplicate,
 *                      else duplicate path before modifying path_arg
 *
 * returns:
 *      < 0     internal error
 *      0       all is OK, path_arg exists as a directory path
 *      > 0     errno error code
 */
static int
mkdirp(char *path_arg, int mode, int duplicate)
{
    struct stat buf;		/* checkpoint directory status */
    char *sep;			// path separator
    int ret;			// mkdir return
    char *path;			// a path to try and create

    /*
     * firewall
     */
    if (path_arg == NULL) {
	warn(__func__, "path_arg is NULL");
	return -1;
    }

    /*
     * quick return of path_arg exists and is a directory
     */
    errno = 0;
    ret = stat(path_arg, &buf);
    if (ret == 0 && errno == 0 && S_ISDIR(buf.st_mode)) {
	/*
	 * path_arg is already a directry - nothing more to do
	 */
	return 0;
    }

    /*
     * duplicae path if needed
     */
    if (duplicate) {
	errno = 0;
	path = strdup(path_arg);
	if (path == NULL) {
	    if (errno == 0) {
		warn(__func__, "failed to mkdir path_arg, errno: 0");
		return CHECKPT_MKDIR_ERRNO_ZERO_ERR;
	    } else {
		warnp(__func__, "failed to mkdir path_arg, errno: %d", errno);
		return errno;
	    }
	}
    } else {
	path = path_arg;
    }

    /*
     * recurse back up the path
     */
    sep = strrchr(path, '/');
    if (sep != NULL) {
	*sep = '\0';
	ret = mkdirp(path, mode, 0);
	*sep = '/';
	if (ret != 0) {
	    /*
	     * some lower level mkdir failed
	     */
	    return ret;
	}
    }

    /*
     * create a path
     */
    errno = 0;
    ret = mkdir(path, mode);
    if (ret < 0) {
	if (errno != EEXIST) {
	    if (errno == 0) {
		warn(__func__, "mkdir %s, errno: %d", path, errno);
		return CHECKPT_MKDIR_ERRNO_ZERO_ERR;
	    } else {
		warnp(__func__, "mkdir %s, errno: %d", path, errno);
		return errno;
	    }
	}
    }

    /*
     * free if we duplicated
     */
    if (duplicate) {
	free(path);
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * setup_checkpt - setup the checkpoint process
 *
 * given:
 *      chkptdir        directory under which checkpoint files will be created,
 *                          NULL ==> do not checkpoint
 *      chkptsecs       checkpoint every chkptsecs seconds, 0 ==> every term,
 *                          <0 ==> do not checkpoint peridocally (only on demand)
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
int
setup_checkpt(char *chkptdir, int chkptsecs)
{
    struct sigaction psa;	/* sigaction info for signal handler setup */
    struct itimerval timer;	/* checkpoint internal */
    int ret;			/* return value */

    /*
     * firewall
     */
    if (chkptdir == NULL) {
	warn(__func__, "chkptdir is NULL");
	return CHECKPT_INVALID_CHECKPT_ARG;
    }

    /*
     * ensure that the checkpoint directory exists that is readable, writable and searchable
     *
     * NOTE: This will verfiy that the checkpoint directory exits, or if it does
     *       not initially exist, attempt to create the checkpoint directory.
     */
    ret = mkdirp(chkptdir, DEF_DIR_MODE, 1);
    if (ret != 0) {
	warn(__func__, "invalid checkpoint directory: %s", chkptdir);
	return ret;
    }
    errno = 0;
    ret = access(chkptdir, W_OK);
    if (ret != 0) {
	if (errno == 0) {
	    warnp(__func__, "cannot write checkpoint directory: %s, errno: 0", chkptdir);
	    return CHECKPT_ACCESS_ERRNO_ZERO_ERR;
	} else if (errno == EACCES) {
	    warn(__func__, "checkpoint directory not writable: %s", chkptdir);
	    return errno;
	} else {
	    warn(__func__, "error while checking for a writable checkpoint directory: %s, errno: %d", chkptdir, errno);
	    return errno;
	}
    }
    errno = 0;
    ret = access(chkptdir, R_OK);
    if (ret != 0) {
	if (errno == 0) {
	    warnp(__func__, "cannot read checkpoint directory: %s, errno: 0", chkptdir);
	    return CHECKPT_ACCESS_ERRNO_ZERO_ERR;
	} else if (errno == EACCES) {
	    warn(__func__, "checkpoint directory not readable: %s", chkptdir);
	    return errno;
	} else {
	    warn(__func__, "error while checking for a readable checkpoint directory: %s, errno: %d", chkptdir, errno);
	    return errno;
	}
    }
    errno = 0;
    ret = access(chkptdir, X_OK);
    if (ret != 0) {
	if (errno == 0) {
	    warnp(__func__, "cannot search checkpoint directory: %s, errno: 0", chkptdir);
	    return CHECKPT_ACCESS_ERRNO_ZERO_ERR;
	} else if (errno == EACCES) {
	    warn(__func__, "checkpoint directory not searchable: %s", chkptdir);
	    return errno;
	} else {
	    warn(__func__, "error while checking for a searchable checkpoint directory: %s, errno: %d", chkptdir, errno);
	    return errno;
	}
    }

    /*
     * move to the checkpoint directory
     */
    errno = 0;
    ret = chdir(chkptdir);
    if (ret != 0) {
	if (errno == 0) {
	    warn(__func__, "cannot cd %s, errno: 0", chkptdir);
	    return CHECKPT_CHDIR_ERRNO_ZERO_ERR;
	} else {
	    warnp(__func__, "cannot cd %s, errno: %d", chkptdir, errno);
	    return errno;
	}
    }

    /*
     * setup SIGALRM and SIGVTALRM handler
     */
    checkpoint_alarm = 0;
    psa.sa_handler = record_sigalarm;
    sigemptyset(&psa.sa_mask);
    psa.sa_flags = 0;
    errno = 0;
    ret = sigaction(SIGALRM, &psa, NULL);
    if (ret != 0) {
	if (errno == 0) {
	    warn(__func__, "cannot sigaction SIGALRM, errno: 0");
	    return CHECKPT_SETACTION_ERRNO_ZERO_ERR;
	} else {
	    warnp(__func__, "cannot sigaction SIGALRM, errno: %d", errno);
	    return errno;
	}
    }
    psa.sa_handler = record_sigalarm;
    sigemptyset(&psa.sa_mask);
    psa.sa_flags = 0;
    errno = 0;
    ret = sigaction(SIGVTALRM, &psa, NULL);
    if (ret != 0) {
	if (errno == 0) {
	    warn(__func__, "cannot sigaction SIGVTALRM, errno: 0");
	    return CHECKPT_SETACTION_ERRNO_ZERO_ERR;
	} else {
	    warnp(__func__, "cannot sigaction SIGVTALRM, errno: %d", errno);
	    return errno;
	}
    }

    /*
     * setup SIGINT handler
     */
    checkpoint_and_end = 0;
    psa.sa_handler = record_sigint;
    sigemptyset(&psa.sa_mask);
    psa.sa_flags = 0;
    errno = 0;
    ret = sigaction(SIGINT, &psa, NULL);
    if (ret != 0) {
	if (errno == 0) {
	    warn(__func__, "cannot sigaction SIGINT, errno: 0");
	    return CHECKPT_SETACTION_ERRNO_ZERO_ERR;
	} else {
	    warnp(__func__, "cannot sigaction SIGINT, errno: %d", errno);
	    return errno;
	}
    }

    /*
     * setup checkpoint interval alarm if chkptsecs > 0
     */
    if (chkptsecs > 0) {
	errno = 0;
	timer.it_interval.tv_sec = chkptsecs;
	timer.it_interval.tv_usec = 0;
	timer.it_value.tv_sec = chkptsecs;
	timer.it_value.tv_usec = 0;
	ret = setitimer(ITIMER_VIRTUAL, &timer, NULL);
	if (ret != 0) {
	    if (errno == 0) {
		warn(__func__, "cannot setitimer ITIMER_VIRTUAL, errno: 0");
		return CHECKPT_SETITIMER_ERRNO_ZERO_ERR;
	    } else {
		warnp(__func__, "cannot setitimer ITIMER_VIRTUAL, errno: %d", errno);
		return errno;
	    }
	}
    }

    /*
     * no errors detected
     */
    return 0;
}


/*
 * checkpt - form a checkpoint file with the current version
 *
 * given:
 *      chkptdir        directory under which checkpoint files will be created
 *      h               multiplier of 2 (h must be >= 1)
 *      n               power of 2      (n must be >= 2)
 *      i               u term index    (i must be >= 2 and <= n)
 *                      NOTE: We assume the 1st Lucas term is U(2) = v(h).
 *                            For Mersenne numbers, U(2) == 4.
 *      u_term          Riesel sequence value: i.e., U(i) where
 *                      U(i+1) = u(i)^2-2 mod h*2^n-1.
 *
 * returns:
 *      0 - no errors detected
 *      < 0 - internal error
 *      > 0 - errno error value
 */
int
checkpt(const char *chkptdir, unsigned long h, unsigned long n, unsigned long i, mpz_t u_term)
{
    /*
     * firewall
     */
    if (chkptdir == NULL) {
	warn(__func__, "chkptdir is NULL");
	return CHECKPT_INVALID_CHECKPT_ARG;
    }
    if (h < 1) {
	warn(__func__, "h must be >= 1: %lu", h);
	return CHECKPT_INVALID_CHECKPT_ARG;
    }
    if (n < 2) {
	warn(__func__, "n must be >= 2: %lu", n);
	return CHECKPT_INVALID_CHECKPT_ARG;
    }
    if (((h % 3 == 1) && (n % 2 == 0)) || ((h % 3 == 2) && (n % 2 == 1))) {
	warn(__func__, "h*2^n-1: %lu*2^%lu-1 must not be a multiple of 3", h, n);
	return CHECKPT_INVALID_CHECKPT_ARG;
    }
    if (i < 2) {
	warn(__func__, "i: %lu must be >= 2", i);
	return CHECKPT_INVALID_CHECKPT_ARG;
    }
    if (i > n) {
	warn(__func__, "i: %lu must be <= n: %lu", i, n);
	return CHECKPT_INVALID_CHECKPT_ARG;
    }
    if (u_term == NULL) {
	warn(__func__, "u_term is NULL");
	return CHECKPT_INVALID_CHECKPT_ARG;
    }

    /*
     * determine checkpoint filename
     */

    /*
     * open checkpoint file
     */

    /*
     * Version 2 checkpoint format
     */

    /*
     * XXX - write this code
     */

    /*
     * no errors detected
     */
    return 0;
}
