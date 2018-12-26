/*
 * checkpoint - checkpoint and restore stilities
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
#include "checkpoint.h"

/*
 * checkpoint flags
 */
uint64_t checkpoint_alarm = 0;		/* != 0 ==> a SIGALRM or SIGVTALRM went off, checkpoint and continue */
uint64_t checkpoint_and_end = 0;	/* != 0 ==> a SIGINT went off, checkpoint and exit */

/*
 * checkpoint filenames
 */
char *filename = NULL;

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
static void careful_write(const char *calling_funcion_name, FILE *stream, char *fmt, ...);
static void write_calc_timeval(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr);
static void write_calc_date_time_str(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr);
static void write_calc_prime_stats_ptr(FILE *stream, char *basename, struct prime_stats *ptr);
static void setup_checkpoint(char *checkpoint_dir, int checkpoint_secs);
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
 *
 * This function does not return on error.
 */
static void
record_sigalarm(int signum)
{
    /*
     * note when non-SIGALRM and non-SIGVTALRM is received
     */
    if (signum != SIGALRM && signum != SIGVTALRM) {
	fflush(stdout);
	fflush(stderr);
	err(99, __func__, "non-SIGALRM and non-SIGVTALRM detected: %d", signum);
	return;	// NOT REACHED
    }

    /*
     * checkpoint at next opportunity
     */
    if (checkpoint_alarm) {
	fflush(stdout);
	fflush(stderr);
	dbg(DBG_LOW, "previous checkpoint_alarm value not cleared: %d", checkpoint_and_end);
    }
    ++checkpoint_alarm;
    if (checkpoint_alarm <= 0) {
	fflush(stdout);
	fflush(stderr);
	dbg(DBG_LOW, "checkpoint_alarm counter wraparound, reset to 1");
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
 *
 * This function does not return on error.
 */
static void
record_sigint(int signum)
{
    /*
     * note when non-SIGALRM is received
     */
    if (signum != SIGINT) {
	fflush(stdout);
	fflush(stderr);
	err(99, __func__, "non-SIGINT detected: %d", signum);
	return;	// NOT REACHED
    }

    /*
     * checkpoint at next opportunity and then exit
     */
    if (checkpoint_and_end) {
	fflush(stdout);
	fflush(stderr);
	dbg(DBG_LOW, "previous checkpoint_and_end value not cleared: %d", checkpoint_and_end);
    }
    ++checkpoint_and_end;
    if (checkpoint_and_end <= 0) {
	fflush(stdout);
	fflush(stderr);
	dbg(DBG_LOW, "checkpoint_and_end counter wraparound, reset to 1");
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
 * This function does not return on error.
 */
static void
zerosize_stats(struct prime_stats *ptr)
{
    /*
     * paranoia
     */
    if (ptr == NULL) {
	err(99, __func__, "ptr is NULL");
	return;	// NOT REACHED
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
 * This function does not return on error.
 */
static void
load_prime_stats(struct prime_stats *ptr)
{
    struct rusage usage;	// our current resource usage

    /*
     * paranoia
     */
    if (ptr == NULL) {
	err(99, __func__, "ptr is NULL");
	return;	// NOT REACHED
    }

    /*
     * zerosize the stats
     */
    zerosize_stats(ptr);

    /*
     * record the time
     */
    if (gettimeofday(&ptr->now, NULL) < 0) {
	errp(99, __func__, "gettimeofday error");
	return;	// NOT REACHED
    }

    /*
     * get resource usage as of now
     */
    memset(&usage, 0, sizeof(usage));
    timerclear(&usage.ru_utime);
    timerclear(&usage.ru_stime);
    if (getrusage(RUSAGE_SELF, &usage) < 0) {
	errp(99, __func__, "getrusage error");
	return;	// NOT REACHED
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
 * This function does not return on error.
 */
static void
careful_write(const char *calling_funcion_name, FILE *stream, char *fmt, ...)
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
	err(99, __func__, "calling_funcion_name is NULL");
	return;	// NOT REACHED
    }
    if (stream == NULL) {
	va_end(ap);		// clean up stdarg stuff
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (fmt == NULL) {
	va_end(ap);		// clean up stdarg stuff
	err(99, __func__, "fmt is NULL");
	return;	// NOT REACHED
    }
    if (fileno(stream) < 0) {
	va_end(ap);		// clean up stdarg stuff
	err(99, __func__, "stream is not valid");
	return;	// NOT REACHED
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
		err(99, __func__,
			"EOF in careful_write called by %s, errno: 0, ret: %d", calling_funcion_name, ret);
	    } else if (ferror(stream)) {
		err(99, __func__,
			"ferror in careful_write called by %s, errno: 0, ret: %d", calling_funcion_name, ret);
	    } else {
		err(99, __func__,
			"error in careful_write called by %s, errno: 0, ret: %d", calling_funcion_name, ret);
	    }
	} else if (feof(stream)) {
	    errp(99, __func__,
		     "EOF in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	} else if (ferror(stream)) {
	    errp(99, __func__,
		     "ferror in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	} else {
	    errp(99, __func__,
		     "error in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	}
	return;	// NOT REACHED
    }
    if (debuglevel >= DBG_VVHIGH) {
	dbg(DBG_VVHIGH, "careful_write called by %s, vfprintf returned: %d", calling_funcion_name, ret);
    }

    /*
     * no errors detected
     */
    return;
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
 * This function does not return on error.
 */
void
write_calc_mpz_hex(FILE *stream, char *basename, char *subname, const mpz_t value)
{
    int ret;	/* mpz_out_str return */

    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(99, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value == NULL) {
	err(99, __func__, "value is NULL");
	return;	// NOT REACHED
    }

    /*
     * write hex variable prefix
     */
    if (basename == NULL) {
	careful_write(__func__, stream, "%s = 0x", subname);
    } else {
	careful_write(__func__, stream, "%s_%s = 0x", basename, subname);
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
		err(99, __func__, "EOF during mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    } else if (ferror(stream)) {
		err(99, __func__, "ferror during mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    } else if (ret == 0) {
		err(99, __func__, "zero return error in mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    } else {
		err(99, __func__, "error during mpz_out_str called from %s, errno: 0, ret: %d", __func__, ret);
	    }
	} else if (feof(stream)) {
	    errp(99, __func__, "EOF during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
	} else if (ferror(stream)) {
	    errp(99, __func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
	} else {
	    errp(99, __func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
	}
	return;	// NOT REACHED
    }

    /*
     * write hex variable suffix
     */
    careful_write(__func__, stream, " ;\n");

    /*
     * no errors detected
     */
    return;
}


/*
 * write_calc_int64_t - write int64_t value to an open stream in calc format
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value - int64_t value to write
 *
 * This function does not return on error.
 */
void
write_calc_int64_t(FILE *stream, char *basename, char *subname, const int64_t value)
{
    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(99, __func__, "subname is NULL");
	return;	// NOT REACHED
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	careful_write(__func__, stream, "%s = %" PRId64 " ;\n", subname, value);
    } else {
	careful_write(__func__, stream, "%s_%s = %" PRId64 " ;\n", basename, subname, value);
    }

    /*
     * no errors detected
     */
    return;
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
 * This function does not return on error.
 */
void
write_calc_uint64_t(FILE *stream, char *basename, char *subname, const uint64_t value)
{
    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(99, __func__, "subname is NULL");
	return;	// NOT REACHED
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	careful_write(__func__, stream, "%s_%s = %" PRIu64 " ;\n", basename, subname, value);
    } else {
	careful_write(__func__, stream, "%s = %" PRIu64 " ;\n", subname, value);
    }

    /*
     * no errors detected
     */
    return;
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
 * This function does not return on error.
 */
void
write_calc_str(FILE *stream, char *basename, char *subname, const char *value)
{
    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(99, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value == NULL) {
	err(99, __func__, "value is NULL");
	return;	// NOT REACHED
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	careful_write(__func__, stream, "%s = \"%s\" ;\n", subname, value);
    } else {
	careful_write(__func__, stream, "%s_%s = \"%s\" ;\n", basename, subname, value);
    }

    /*
     * no errors detected
     */
    return;
}


/*
 * write_calc_timeval - write a time value in seconds.microseconds to an open stream in calc format
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value_ptr - pointer to a struct timeval
 *
 * This function does not return on error.
 */
static void
write_calc_timeval(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr)
{
    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(99, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value_ptr == NULL) {
	err(99, __func__, "value_ptr is NULL");
	return;	// NOT REACHED
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	careful_write(__func__, stream, "%s = %" PRIu64 ".%06d ;\n", subname, value_ptr->tv_sec, value_ptr->tv_usec);
    } else {
	careful_write(__func__, stream, "%s_%s = %" PRIu64 ".%06d ;\n",
			        basename, subname, value_ptr->tv_sec, value_ptr->tv_usec);
    }

    /*
     * no errors detected
     */
    return;
}


/*
 * write_calc_date_time_str - write a time value in date time string to an open stream in calc format
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name (NULL ==> just use subname)
 *      subname - subcomponent variable name
 *      value_ptr - pointer to a struct timeval
 *
 * This function does not return on error.
 */
static void
write_calc_date_time_str(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr)
{
    struct tm *tm_time;		/* broken-down time */
    char buf[BUFSIZ + 1];	/* time string buffer */
    int ret;			/* gmtime() and strftime() return */

    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(99, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value_ptr == NULL) {
	err(99, __func__, "value_ptr is NULL");
	return;	// NOT REACHED
    }

    /*
     * convert timeval to broken-down time
     */
    tm_time = gmtime(&value_ptr->tv_sec);
    if (tm_time == NULL) {
	if (errno == 0) {
	    err(99, __func__, "gmtime returned NULL, errno: 0");
	} else {
	    errp(99, __func__, "gmtime returned NULL, errno: %d", errno);
	}
	return;	// NOT REACHED
    }

    /*
     * format as a date and time string
     */
    ret = strftime(buf, BUFSIZ, "%F %T UTC", tm_time);
    if (ret <= 0) {
	if (errno == 0) {
	    errp(99, __func__, "strftime returned %d, errno = 0", ret);
	} else {
	    errp(99, __func__, "strftime returned %d, errno = %d", ret, errno);
	}
	return;	// NOT REACHED
    }
    buf[BUFSIZ] = '\0';		// paranoia

    /*
     * write date and time as a calc string
     */
    write_calc_str(stream, basename, subname, buf);

    /*
     * no errors detected
     */
    return;
}


/*
 * write_calc_prime_stats_ptr - write prime stats to an open stream in calc format
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      basename - base variable name
 *      value_ptr - pointer to a struct prime_stats
 *
 * This function does not return on error.
 */
static void
write_calc_prime_stats_ptr(FILE *stream, char *basename, struct prime_stats *ptr)
{
    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (basename == NULL) {
	err(99, __func__, "basename is NULL");
	return;	// NOT REACHED
    }
    if (ptr == NULL) {
	err(99, __func__, "ptr is NULL");
	return;	// NOT REACHED
    }

    /*
     * write start time as a timestamp since the epoch
     */
    write_calc_timeval(stream, basename, "timestamp", &ptr->now);

    /*
     * write start time as a timestamp since as a string
     */
    write_calc_date_time_str(stream, basename, "date_time", &ptr->now);

    /*
     * write user CPU time used
     */
    write_calc_timeval(stream, basename, "ru_utime", &ptr->ru_utime);

    /*
     * write system CPU time used
     */
    write_calc_timeval(stream, basename, "ru_stime", &ptr->ru_stime);

    /*
     * write wall clock time used
     */
    write_calc_timeval(stream, basename, "wall_clock", &ptr->wall_clock);

    /*
     * write maximum resident set size used in kilobytes
     */
    write_calc_int64_t(stream, basename, "ru_maxrss", ptr->ru_maxrss);

    /*
     * write page reclaims (soft page faults)
     */
    write_calc_int64_t(stream, basename, "ru_minflt", ptr->ru_minflt);

    /*
     * write page faults (hard page faults)
     */
    write_calc_int64_t(stream, basename, "ru_majflt", ptr->ru_majflt);

    /*
     * write block input operations
     */
    write_calc_int64_t(stream, basename, "ru_inblock", ptr->ru_inblock);

    /*
     * write block output operations
     */
    write_calc_int64_t(stream, basename, "ru_oublock", ptr->ru_oublock);

    /*
     * write voluntary context switches
     */
    write_calc_int64_t(stream, basename, "ru_nvcsw", ptr->ru_nvcsw);

    /*
     * write involuntary context switches
     */
    write_calc_int64_t(stream, basename, "ru_nivcsw", ptr->ru_nivcsw);

    /*
     * no errors detected
     */
    return;
}


/*
 * write_calc_prime_stats - write prime stats to an open stream in calc format
 *
 * given:
 *      stream - open checkpoint file stream to append to
 *      extended - 0 ==> write only total stats, != 0 ==> write all stats
 *
 * This function does not return on error.
 */
void
write_calc_prime_stats(FILE *stream, int extended)
{
    /*
     * firewall
     */
    if (stream == NULL) {
	err(99, __func__, "stream is NULL");
	return;	// NOT REACHED
    }

    /*
     * if -t was given
     */
    if (extended) {

	/*
	 * write beginrun stats
	 */
	write_calc_prime_stats_ptr(stream, "beginrun", &beginrun);

	/*
	 * write current stats
	 */
	write_calc_prime_stats_ptr(stream, "current", &current);

	/*
	 * write restored stats
	 */
	write_calc_prime_stats_ptr(stream, "restored", &restored);
    }

    /*
     * write total stats
     */
    write_calc_prime_stats_ptr(stream, "total", &total);

    /*
     * no errors detected
     */
    return;
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
 * NOTE: This function assumes that initialize_beginrun_stats() was already
 *	 called and that beginrun contains info from the start of this program.
 */
void
initialize_total_stats(void)
{
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
 * initialize_checkpoint - setup checkpoint system
 *
 * given:
 *      checkpoint_dir        directory under which checkpoint files will be created
 *                          	NULL ==> do not checkpoint
 *      checkpoint_secs       checkpoint every checkpoint_secs seconds, 0 ==> every term,
 *                          	<0 ==> do not checkpoint periodically (only on demand)
 *
 * This function does not return on error.
 */
void
initialize_checkpoint(char *checkpoint_dir, int checkpoint_secs)
{
    /*
     * if we have a non-NULL checkpoint_dir, setup the checkpoint system
     */
    if (checkpoint_dir != NULL) {
	setup_checkpoint(checkpoint_dir, checkpoint_secs);
    }

    /* XXX - more code here */

    /*
     * checkpoint system has been initialized
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
	err(99, __func__, "path_arg is NULL");
	return -1; // NOT REACHED
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
		err(99, __func__, "failed to mkdir path_arg, errno: 0");
	    } else {
		errp(99, __func__, "failed to mkdir path_arg, errno: %d", errno);
	    }
	    return -1; // NOT REACHED
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
		err(99, __func__, "mkdir %s, errno: %d", path, errno);
	    } else {
		errp(99, __func__, "mkdir %s, errno: %d", path, errno);
	    }
	    return -1; // NOT REACHED
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
 * setup_checkpoint - setup the checkpoint system
 *
 * given:
 *      checkpoint_dir        directory under which checkpoint files will be created,
 *                          	NULL ==> do not checkpoint
 *      checkpoint_secs       checkpoint every checkpoint_secs seconds, 0 ==> every term,
 *                          	<0 ==> do not checkpoint periodically (only on demand)
 *
 * This function does not return on error.
 */
static void
setup_checkpoint(char *checkpoint_dir, int checkpoint_secs)
{
    struct sigaction psa;	/* sigaction info for signal handler setup */
    struct itimerval timer;	/* checkpoint internal */
    int ret;			/* return value */

    /*
     * firewall
     */
    if (checkpoint_dir == NULL) {
	err(99, __func__, "checkpoint_dir is NULL");
	return;	// NOT REACHED
    }

    /*
     * ensure that the checkpoint directory exists that is readable, writable and searchable
     *
     * NOTE: This will verfiy that the checkpoint directory exits, or if it does
     *       not initially exist, attempt to create the checkpoint directory.
     */
    ret = mkdirp(checkpoint_dir, DEF_DIR_MODE, 1);
    if (ret != 0) {
	err(99, __func__, "invalid checkpoint directory: %s", checkpoint_dir);
	return;	// NOT REACHED
    }
    errno = 0;
    ret = access(checkpoint_dir, W_OK);
    if (ret != 0) {
	if (errno == 0) {
	    errp(99, __func__, "cannot write checkpoint directory: %s, errno: 0", checkpoint_dir);
	} else if (errno == EACCES) {
	    err(99, __func__, "checkpoint directory not writable: %s", checkpoint_dir);
	} else {
	    err(99, __func__, "error while checking for a writable checkpoint directory: %s, errno: %d", checkpoint_dir, errno);
	}
	return;	// NOT REACHED
    }
    errno = 0;
    ret = access(checkpoint_dir, R_OK);
    if (ret != 0) {
	if (errno == 0) {
	    errp(99, __func__, "cannot read checkpoint directory: %s, errno: 0", checkpoint_dir);
	} else if (errno == EACCES) {
	    err(99, __func__, "checkpoint directory not readable: %s", checkpoint_dir);
	} else {
	    err(99, __func__, "error while checking for a readable checkpoint directory: %s, errno: %d", checkpoint_dir, errno);
	}
	return;	// NOT REACHED
    }
    errno = 0;
    ret = access(checkpoint_dir, X_OK);
    if (ret != 0) {
	if (errno == 0) {
	    errp(99, __func__, "cannot search checkpoint directory: %s, errno: 0", checkpoint_dir);
	} else if (errno == EACCES) {
	    err(99, __func__, "checkpoint directory not searchable: %s", checkpoint_dir);
	} else {
	    err(99, __func__, "error while checking for a searchable checkpoint directory: %s, errno: %d", checkpoint_dir, errno);
	}
	return;	// NOT REACHED
    }

    /*
     * move to the checkpoint directory
     */
    errno = 0;
    ret = chdir(checkpoint_dir);
    if (ret != 0) {
	if (errno == 0) {
	    err(99, __func__, "cannot cd %s, errno: 0", checkpoint_dir);
	} else {
	    errp(99, __func__, "cannot cd %s, errno: %d", checkpoint_dir, errno);
	}
	return;	// NOT REACHED
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
	    err(99, __func__, "cannot sigaction SIGALRM, errno: 0");
	} else {
	    errp(99, __func__, "cannot sigaction SIGALRM, errno: %d", errno);
	}
	return;	// NOT REACHED
    }
    psa.sa_handler = record_sigalarm;
    sigemptyset(&psa.sa_mask);
    psa.sa_flags = 0;
    errno = 0;
    ret = sigaction(SIGVTALRM, &psa, NULL);
    if (ret != 0) {
	if (errno == 0) {
	    err(99, __func__, "cannot sigaction SIGVTALRM, errno: 0");
	} else {
	    errp(99, __func__, "cannot sigaction SIGVTALRM, errno: %d", errno);
	}
	return;	// NOT REACHED
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
	    err(99, __func__, "cannot sigaction SIGINT, errno: 0");
	} else {
	    errp(99, __func__, "cannot sigaction SIGINT, errno: %d", errno);
	}
	return;	// NOT REACHED
    }

    /*
     * setup checkpoint interval alarm if checkpoint_secs > 0
     */
    if (checkpoint_secs > 0) {
	errno = 0;
	timer.it_interval.tv_sec = checkpoint_secs;
	timer.it_interval.tv_usec = 0;
	timer.it_value.tv_sec = checkpoint_secs;
	timer.it_value.tv_usec = 0;
	ret = setitimer(ITIMER_VIRTUAL, &timer, NULL);
	if (ret != 0) {
	    if (errno == 0) {
		err(99, __func__, "cannot setitimer ITIMER_VIRTUAL, errno: 0");
	    } else {
		errp(99, __func__, "cannot setitimer ITIMER_VIRTUAL, errno: %d", errno);
	    }
	    return;	// NOT REACHED
	}
    }

    /*
     * no errors detected
     */
    return;
}


/*
 * checkpoint - form a checkpoint file with the current version
 *
 * given:
 *      checkpoint_dir        directory under which checkpoint files will be created
 *      h               multiplier of 2 (h must be >= 1)
 *      n               power of 2      (n must be >= 2)
 *      i               u term index    (i must be >= 2 and <= n)
 *                      NOTE: We assume the 1st Lucas term is U(2) = v(h).
 *                            For Mersenne numbers, U(2) == 4.
 *      u_term          Riesel sequence value: i.e., U(i) where
 *                      U(i+1) = u(i)^2-2 mod h*2^n-1.
 *
 * This function does not return on error.
 */
void
checkpoint(const char *checkpoint_dir, unsigned long h, unsigned long n, unsigned long i, mpz_t u_term)
{
    /*
     * firewall
     */
    if (checkpoint_dir == NULL) {
	err(99, __func__, "checkpoint_dir is NULL");
	return;	// NOT REACHED
    }
    if (h < 1) {
	err(99, __func__, "h must be >= 1: %lu", h);
	return;	// NOT REACHED
    }
    if (n < 2) {
	err(99, __func__, "n must be >= 2: %lu", n);
	return;	// NOT REACHED
    }
    if (((h % 3 == 1) && (n % 2 == 0)) || ((h % 3 == 2) && (n % 2 == 1))) {
	err(99, __func__, "h*2^n-1: %lu*2^%lu-1 must not be a multiple of 3", h, n);
	return;	// NOT REACHED
    }
    if (i < 2) {
	err(99, __func__, "i: %lu must be >= 2", i);
	return;	// NOT REACHED
    }
    if (i > n) {
	err(99, __func__, "i: %lu must be <= n: %lu", i, n);
	return;	// NOT REACHED
    }
    if (u_term == NULL) {
	err(99, __func__, "u_term is NULL");
	return;	// NOT REACHED
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
    return;
}
