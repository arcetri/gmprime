/*
 * checkpoint - checkpoint and restore stilities
 *
 * Copyright (c) 2018-2020 by Landon Curt Noll.  All Rights Reserved.
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

/* NUMERIC EXIT CODES: 70-99	checkpoint.c - reserved for internal errors */

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
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdbool.h>
#include <bits/local_lim.h>

#include "gmprime.h"
#include "riesel.h"
#include "debug.h"
#include "checkpoint.h"

/*
 * checkpoint flags
 */
uint64_t checkpoint_alarm = 0;		/* != 0 ==> a SIGALRM or SIGVTALRM went off, checkpoint and continue */
// XXX - catch SIGHUP and perhaps other signals?
uint64_t checkpoint_and_end = 0;	/* != 0 ==> a SIGINT went off, checkpoint and exit */

/*
 * checkpoint strings values
 */
static pid_t pid;				/* our process ID */
static pid_t ppid;				/* our parent's process ID */
static char hostname[HOST_NAME_MAX+1];		/* our hostname */
static char cwd[PATH_MAX+1];			/* our current working directory */

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
static void record_sighup(int signum);
static void zerosize_stats(struct prime_stats *ptr);
static void load_prime_stats(struct prime_stats *ptr);
static void careful_write(const char *calling_funcion_name, FILE *stream, char *fmt, ...);
static void write_calc_timeval(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr);
static void write_calc_date_time_str(FILE *stream, char *basename, char *subname, const struct timeval *value_ptr);
static void write_calc_prime_stats_ptr(FILE *stream, char *basename, struct prime_stats *ptr);
static void initialize_total_stats(void);
static void setup_checkpoint(char *checkpoint_dir, int checkpoint_secs);
static int mkdirp(char *path_arg, int mode, int duplicate);
static void setup_chkpt_links(unsigned long h, unsigned long n, unsigned long i, mpz_t u_term);


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
	err(70, __func__, "non-SIGALRM and non-SIGVTALRM detected: %d", signum);
	return;	// NOT REACHED
    }

    /*
     * checkpoint at next opportunity
     */
    if (checkpoint_alarm) {
	fflush(stdout);
	fflush(stderr);
	dbg(DBG_LOW, "previous checkpoint_alarm value not cleared: %ld", checkpoint_and_end);
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
	err(71, __func__, "non-SIGINT detected: %d", signum);
	return;	// NOT REACHED
    }

    /*
     * checkpoint at next opportunity and then exit
     */
    if (checkpoint_and_end) {
	fflush(stdout);
	fflush(stderr);
	dbg(DBG_LOW, "previous checkpoint_and_end value not cleared: %ld", checkpoint_and_end);
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
 * record_sighup - record a SIGHUP
 *
 * given:
 *      signum          the signal that has been delivered
 *
 * This is the signal handler for the SIGHUP.
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
record_sighup(int signum)
{
    /*
     * note when non-SIGALRM is received
     */
    if (signum != SIGHUP) {
	fflush(stdout);
	fflush(stderr);
	err(71, __func__, "non-SIGHUP detected: %d", signum);
	return;	// NOT REACHED
    }

    /*
     * checkpoint at next opportunity and then exit
     */
    if (checkpoint_and_end) {
	fflush(stdout);
	fflush(stderr);
	dbg(DBG_LOW, "previous checkpoint_and_end value not cleared: %ld", checkpoint_and_end);
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
	err(72, __func__, "ptr is NULL");
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
	err(73, __func__, "ptr is NULL");
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
	errp(73, __func__, "gettimeofday error");
	return;	// NOT REACHED
    }

    /*
     * get resource usage as of now
     */
    memset(&usage, 0, sizeof(usage));
    timerclear(&usage.ru_utime);
    timerclear(&usage.ru_stime);
    if (getrusage(RUSAGE_SELF, &usage) < 0) {
	errp(73, __func__, "getrusage error");
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
	err(74, __func__, "calling_funcion_name is NULL");
	return;	// NOT REACHED
    }
    if (stream == NULL) {
	va_end(ap);		// clean up stdarg stuff
	err(74, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (fmt == NULL) {
	va_end(ap);		// clean up stdarg stuff
	err(74, __func__, "fmt is NULL");
	return;	// NOT REACHED
    }
    if (fileno(stream) < 0) {
	va_end(ap);		// clean up stdarg stuff
	err(74, __func__, "stream is not valid");
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
    if (ret <= 0 || ferror(stream) || feof(stream)) {
	if (feof(stream)) {
	    errp(74, __func__,
		     "EOF in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	} else if (ferror(stream)) {
	    errp(74, __func__,
		     "ferror in careful_write called by %s, errno: %d, ret: %d", calling_funcion_name, errno, ret);
	} else {
	    errp(74, __func__,
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
	err(75, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(75, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value == NULL) {
	err(75, __func__, "value is NULL");
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
    if (ret <= 0 || ferror(stream) || feof(stream)) {
	if (feof(stream)) {
	    errp(75, __func__, "EOF during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
	} else if (ferror(stream)) {
	    errp(75, __func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
	} else {
	    errp(75, __func__, "ferror during mpz_out_str called from %s, errno: %d, ret: %d", __func__, errno, ret);
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
	err(76, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(76, __func__, "subname is NULL");
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
	err(77, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(77, __func__, "subname is NULL");
	return;	// NOT REACHED
    }

    /*
     * write the calc expression
     */
    if (basename == NULL) {
	careful_write(__func__, stream, "%s = %" PRIu64 " ;\n", subname, value);
    } else {
	careful_write(__func__, stream, "%s_%s = %" PRIu64 " ;\n", basename, subname, value);
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
	err(78, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(78, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value == NULL) {
	err(78, __func__, "value is NULL");
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
	err(79, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(79, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value_ptr == NULL) {
	err(79, __func__, "value_ptr is NULL");
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
	err(80, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (subname == NULL) {
	err(80, __func__, "subname is NULL");
	return;	// NOT REACHED
    }
    if (value_ptr == NULL) {
	err(80, __func__, "value_ptr is NULL");
	return;	// NOT REACHED
    }

    /*
     * convert timeval to broken-down time
     */
    tm_time = gmtime(&value_ptr->tv_sec);
    if (tm_time == NULL) {
	errp(80, __func__, "gmtime returned NULL, errno: %d", errno);
	return;	// NOT REACHED
    }

    /*
     * format as a date and time string
     */
    memset(buf, 0, sizeof(buf));
    ret = strftime(buf, BUFSIZ, "%F %T UTC", tm_time);
    if (ret <= 0) {
	errp(80, __func__, "strftime returned %d, errno: %d", ret, errno);
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
	err(81, __func__, "stream is NULL");
	return;	// NOT REACHED
    }
    if (basename == NULL) {
	err(81, __func__, "basename is NULL");
	return;	// NOT REACHED
    }
    if (ptr == NULL) {
	err(81, __func__, "ptr is NULL");
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
 *      extended - true ==> write only total stats, false ==> write all stats
 *
 * This function does not return on error.
 */
void
write_calc_prime_stats(FILE *stream, bool extended)
{
    /*
     * firewall
     */
    if (stream == NULL) {
	err(82, __func__, "stream is NULL");
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
static void
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
 *      h               multiplier of 2
 *      n               power of 2
 *      force		true ==> unless locked we force checkpoint to be initialized
 *
 * This funcion does not check or use h and n if checkpoint_dir == NULL (no checkpointing).
 *
 * If checkpoint_dir is not NULL, this function will call setup_checkpoint to be sure the
 * checkpoint file exists, and then to lock the checkpoint directory.
 *
 * This funcion will always initialize timer stats at the start.
 *
 * If we restore from a checkpoint file later on,
 * after the checkpoint system has been initialixzed
 * and the most recent valid checkpont file is found,
 * we may modify the total stats as per that checkpoint.
 * If and until that happens, we will pretend the
 * total prime stats started with this run.
 *
 * This function does not return on error.
 */
void
initialize_checkpoint(char *checkpoint_dir, int checkpoint_secs, unsigned long h, unsigned long n, bool force)
{
    int f_ret;		// function return value

    /*
     * initialize timer stats
     */
    initialize_total_stats();

    /*
     * firewall only if checkpoint_dir is no NULL
     */
    if (checkpoint_dir != NULL) {

	/*
	 * non-restore firewall
	 */
	if (h < 1) {
	    err(83, __func__, "h must be >= 1: %lu", h);
	    return;	// NOT REACHED
	}
	if (n < 2) {
	    err(83, __func__, "n must be >= 2: %lu", n);
	    return;	// NOT REACHED
	}

	/*
	 * be sure checkpoint directory exits and is locked
	 */
	setup_checkpoint(checkpoint_dir, checkpoint_secs);

	/*
	 * setup save and result links, if needed
	 */
	setup_chkpt_links(h, n, 0, NULL);

	/*
	 * if result.prime.pt exists, exit showing we found a prime unless forcing
	 */
	errno = 0;
	f_ret = access(RESULT_PRIME_FILE, F_OK);
	if (f_ret == 0) {
	    /* RESULT_PRIME_FILE exists */
	    if (force) {
		dbg(DBG_LOW, "rm -f %s", RESULT_PRIME_FILE);
		errno = 0;
		f_ret = unlink(RESULT_PRIME_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", RESULT_PRIME_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    } else {
		err(EXIT_IS_PRIME, __func__, "%s exists, already proven", RESULT_PRIME_FILE);
		// exit(0);
		exit(EXIT_IS_PRIME);	// NOT REACHED
	    }
	}

	/*
	 * if result.composite.pt exists, exit showing we found a composite
	 */
	errno = 0;
	f_ret = access(RESULT_COMPOSITE_FILE, F_OK);
	if (f_ret == 0) {
	    /* RESULT_COMPOSITE_FILE exists */
	    if (force) {
		dbg(DBG_LOW, "rm -f %s", RESULT_COMPOSITE_FILE);
		errno = 0;
		f_ret = unlink(RESULT_COMPOSITE_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", RESULT_COMPOSITE_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    } else {
		err(EXIT_IS_COMPOSITE, __func__, "%s exists, already proven", RESULT_COMPOSITE_FILE);
		// exit(1);
		exit(EXIT_IS_COMPOSITE);	// NOT REACHED
	    }
	}

	/*
	 * if result.error.pt exists, exit showing there was a fatal error preventing testing
	 */
	errno = 0;
	f_ret = access(RESULT_ERROR_FILE, F_OK);
	if (f_ret == 0) {
	    /* RESULT_ERROR_FILE exists */
	    if (force) {
		dbg(DBG_LOW, "rm -f %s", RESULT_ERROR_FILE);
		errno = 0;
		f_ret = unlink(RESULT_ERROR_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", RESULT_ERROR_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    } else {
		err(EXIT_CANNOT_RESTORE, __func__, "%s exists, cannot prove right now", RESULT_ERROR_FILE);
		// exit(6);
		exit(EXIT_CANNOT_RESTORE);	// NOT REACHED
	    }
	}

	/*
	 * if sav.end.pt exists, but no result.*.pt file, we have an error
	 */
	errno = 0;
	f_ret = access(SAVE_END_FILE, F_OK);
	if (f_ret == 0) {
	    /* SAVE_END_FILE exists */
	    if (force) {
		dbg(DBG_LOW, "rm -f %s", SAVE_END_FILE);
		errno = 0;
		f_ret = unlink(SAVE_END_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", SAVE_END_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    } else {
		err(EXIT_CANNOT_RESTORE, __func__, "%s exists, but no %s nor %s nor %s",
				      SAVE_END_FILE, RESULT_PRIME_FILE, RESULT_COMPOSITE_FILE, RESULT_ERROR_FILE);
		// exit(6);
		exit(EXIT_CANNOT_RESTORE);	// NOT REACHED
	    }
	}

	/*
	 * if forced, then remove chk.* files and sav.u2.pt
	 */
    	if (force) {

	    /*
	     * force remove SAVE_FIRST_FILE if it exists
	     */
	    errno = 0;
	    f_ret = access(SAVE_FIRST_FILE, F_OK);
	    if (f_ret == 0) {
		/* SAVE_FIRST_FILE exists */
		dbg(DBG_LOW, "rm -f %s", SAVE_FIRST_FILE);
		errno = 0;
		f_ret = unlink(SAVE_FIRST_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", SAVE_FIRST_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    }

	    /*
	     * force remove CHKPT_CUR_FILE if it exists
	     */
	    errno = 0;
	    f_ret = access(CHKPT_CUR_FILE, F_OK);
	    if (f_ret == 0) {
		/* CHKPT_CUR_FILE exists */
		dbg(DBG_LOW, "rm -f %s", CHKPT_CUR_FILE);
		errno = 0;
		f_ret = unlink(CHKPT_CUR_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", CHKPT_CUR_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    }

	    /*
	     * force remove CHKPT_PREV0_FILE if it exists
	     */
	    errno = 0;
	    f_ret = access(CHKPT_PREV0_FILE, F_OK);
	    if (f_ret == 0) {
		/* CHKPT_CUR_FILE exists */
		dbg(DBG_LOW, "rm -f %s", CHKPT_PREV0_FILE);
		errno = 0;
		f_ret = unlink(CHKPT_PREV0_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", CHKPT_PREV0_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    }

	    /*
	     * force remove CHKPT_PREV1_FILE if it exists
	     */
	    errno = 0;
	    f_ret = access(CHKPT_PREV1_FILE, F_OK);
	    if (f_ret == 0) {
		/* CHKPT_PREV1_FILE exists */
		dbg(DBG_LOW, "rm -f %s", CHKPT_PREV1_FILE);
		errno = 0;
		f_ret = unlink(CHKPT_PREV1_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", CHKPT_PREV1_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    }

	    /*
	     * force remove CHKPT_PREV2_FILE if it exists
	     */
	    errno = 0;
	    f_ret = access(CHKPT_PREV2_FILE, F_OK);
	    if (f_ret == 0) {
		/* CHKPT_PREV2_FILE exists */
		dbg(DBG_LOW, "rm -f %s", CHKPT_PREV2_FILE);
		errno = 0;
		f_ret = unlink(CHKPT_PREV2_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", CHKPT_PREV2_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    }

	    /*
	     * force remove SAVE_NEAR_FILE if it exists
	     */
	    errno = 0;
	    f_ret = access(SAVE_NEAR_FILE, F_OK);
	    if (f_ret == 0) {
		/* SAVE_NEAR_FILE exists */
		dbg(DBG_LOW, "rm -f %s", SAVE_NEAR_FILE);
		errno = 0;
		f_ret = unlink(SAVE_NEAR_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", SAVE_NEAR_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    }

	    /*
	     * force remove SAVE_N1_FILE if it exists
	     */
	    errno = 0;
	    f_ret = access(SAVE_N1_FILE, F_OK);
	    if (f_ret == 0) {
		/* SAVE_N1_FILE exists */
		dbg(DBG_LOW, "rm -f %s", SAVE_N1_FILE);
		errno = 0;
		f_ret = unlink(SAVE_N1_FILE);
		if (f_ret < 0) {
		    err(EXIT_CHKPT_ACCESS, __func__, "cannot remove %s", SAVE_N1_FILE);
		    // exit(4);
		    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
		}
	    }
	}
    }

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
	err(84, __func__, "path_arg is NULL");
	return -1; // NOT REACHED
    }

    /*
     * quick return of path_arg exists and is a directory
     */
    errno = 0;
    ret = stat(path_arg, &buf);
    if (ret == 0 && S_ISDIR(buf.st_mode)) {
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
	    errp(84, __func__, "failed to mkdir path_arg, errno: %d", errno);
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
	    errp(84, __func__, "mkdir %s, errno: %d", path, errno);
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
 * This function will create (if needed) and lock the LOCK_FILE lock file.
 * This function will also set the pid and ppid values.
 * This function will also set the cwd[] and hostname[] strings.
 *
 * This function does not return on error.
 */
static void
setup_checkpoint(char *checkpoint_dir, int checkpoint_secs)
{
    FILE *stream;		// opened lock file
    struct sigaction psa;	/* sigaction info for signal handler setup */
    struct itimerval timer;	/* checkpoint internal */
    int fd;			/* open lock file */
    int ret;			/* return value */
    char *cwd_ret;		/* return from getcwd() */

    /*
     * firewall
     */
    if (checkpoint_dir == NULL) {
	err(85, __func__, "checkpoint_dir is NULL");
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
	err(EXIT_CHKPT_ACCESS, __func__, "invalid checkpoint directory: %s", checkpoint_dir);
	// exit(4);
	exit(EXIT_CHKPT_ACCESS); // NOT REACHED
	return;	// NOT REACHED
    }
    errno = 0;
    ret = access(checkpoint_dir, W_OK);
    if (ret != 0) {
	if (errno == EACCES) {
	    err(EXIT_CHKPT_ACCESS, __func__, "checkpoint directory not writable: %s, errno: %d", checkpoint_dir, errno);
	    // exit(4);
	} else {
	    errp(EXIT_CHKPT_ACCESS, __func__, "error while checking for a writable checkpoint directory: %s, errno: %d",
	    			    checkpoint_dir, errno);
	    // exit(4);
	}
	exit(EXIT_CHKPT_ACCESS); // NOT REACHED
	return;	// NOT REACHED
    }
    errno = 0;
    ret = access(checkpoint_dir, R_OK);
    if (ret != 0) {
	if (errno == EACCES) {
	    err(EXIT_CHKPT_ACCESS, __func__, "checkpoint directory not readable: %s, errno: %d", checkpoint_dir, errno);
	    // exit(4);
	} else {
	    errp(EXIT_CHKPT_ACCESS, __func__, "error while checking for a readable checkpoint directory: %s, errno: %d",
	    			    checkpoint_dir, errno);
	    // exit(4);
	}
	exit(EXIT_CHKPT_ACCESS); // NOT REACHED
	return;	// NOT REACHED
    }
    errno = 0;
    ret = access(checkpoint_dir, X_OK);
    if (ret != 0) {
	if (errno == EACCES) {
	    err(EXIT_CHKPT_ACCESS, __func__, "checkpoint directory not searchable: %s errno: %d", checkpoint_dir, errno);
	    // exit(4);
	} else {
	    errp(EXIT_CHKPT_ACCESS, __func__, "error while checking for a searchable checkpoint directory: %s, errno: %d",
	    			    checkpoint_dir, errno);
	    // exit(4);
	}
	exit(EXIT_CHKPT_ACCESS); // NOT REACHED
	return;	// NOT REACHED
    }

    /*
     * move to the checkpoint directory
     */
    errno = 0;
    ret = chdir(checkpoint_dir);
    if (ret != 0) {
	errp(EXIT_CHKPT_ACCESS, __func__, "cannot cd %s, errno: %d", checkpoint_dir, errno);
	// exit(4);
	exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	return;	// NOT REACHED
    }

    /*
     * determine the current working directory
     */
    memset(cwd, 0, sizeof(cwd));
    errno = 0;
    cwd_ret = getcwd(cwd, PATH_MAX);
    if (cwd_ret == NULL) {
	errp(EXIT_CHKPT_ACCESS, __func__, "error tring to determine the current working directory");
	// exit(4);
	exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	return;	// NOT REACHED
    }
    cwd[PATH_MAX] = '\0'; // paranoia

    /*
     * open lock file, creating as needed
     */
    errno = 0;
    fd = open(LOCK_FILE, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
    if (fd < 0) {
	errp(EXIT_CHKPT_ACCESS, __func__, "cannot open %s/%s, errno: %d", checkpoint_dir, LOCK_FILE, errno);
	// exit(4);
	exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	return;	// NOT REACHED
    }
    errno = 0;
    stream = fdopen(fd, "w");
    if (stream == NULL) {
	errp(EXIT_CHKPT_ACCESS, __func__, "cannot fdopen(%d, \"w\"): %s/%s, errno: %d", fd, checkpoint_dir, LOCK_FILE, errno);
	// exit(4);
	exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	return;	// NOT REACHED
    }

    /*
     * lock the lock file or exit
     */
    errno = 0;
    ret = flock(fd, LOCK_EX|LOCK_NB);
    if (ret < 0) {
    	if (errno == EWOULDBLOCK) {
	    dbg(DBG_LOW, "already locked %s/%s, errno: %d, exiting", checkpoint_dir, LOCK_FILE, errno);
	    err(EXIT_LOCKED, __func__, "checkpoint directory locked by another process");
	    // exit(5);
	    exit(EXIT_LOCKED);	// NOT REACHED
	} else {
	    errp(EXIT_CHKPT_ACCESS, __func__, "error in locking %s/%s, errno: %d", checkpoint_dir, LOCK_FILE, errno);
	    // exit(4);
	    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	}
	return;	// NOT REACHED
    }

    /*
     * determine our hostname
     */
    memset(hostname, 0, sizeof(hostname));
    errno = 0;
    ret = gethostname(hostname, HOST_NAME_MAX);
    if (ret < 0) {
	errp(85, __func__, "gethostname returned %d, errno: %d", ret, errno);
	return;	// NOT REACHED
    }
    hostname[HOST_NAME_MAX] = '\0'; // paranoia
    dbg(DBG_MED, "hostname: %s", hostname);

    /*
     * determine process IDs
     */
    pid = getpid();
    dbg(DBG_MED, "pid: %d", pid);
    ppid = getppid();
    dbg(DBG_MED, "ppid: %d", ppid);

    /*
     * write execution info into the lock file
     */
    write_calc_int64_t(stream, NULL, "version", CHECKPOINT_FMT_VERSION);
    hostname[HOST_NAME_MAX] = '\0'; // paranoia
    write_calc_str(stream, NULL, "hostname", hostname);
    cwd[HOST_NAME_MAX] = '\0'; // paranoia
    write_calc_str(stream, NULL, "cwd", cwd);
    write_calc_str(stream, NULL, "checkpoint_dir", checkpoint_dir);
    write_calc_uint64_t(stream, NULL, "pid", pid);
    write_calc_uint64_t(stream, NULL, "ppid", ppid);
    load_prime_stats(&current);
    write_calc_prime_stats_ptr(stream, "locktime", &current);
    write_calc_str(stream, NULL, "complete", "true");
    fflush(stream); // paranoia

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
	errp(85, __func__, "cannot sigaction SIGALRM, errno: %d", errno);
	return;	// NOT REACHED
    }
    psa.sa_handler = record_sigalarm;
    sigemptyset(&psa.sa_mask);
    psa.sa_flags = 0;
    errno = 0;
    ret = sigaction(SIGVTALRM, &psa, NULL);
    if (ret != 0) {
	errp(85, __func__, "cannot sigaction SIGVTALRM, errno: %d", errno);
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
	errp(85, __func__, "cannot sigaction SIGINT, errno: %d", errno);
	return;	// NOT REACHED
    }

    /*
     * setup SIGHUP handler
     */
    checkpoint_and_end = 0;
    psa.sa_handler = record_sighup;
    sigemptyset(&psa.sa_mask);
    psa.sa_flags = 0;
    errno = 0;
    ret = sigaction(SIGHUP, &psa, NULL);
    if (ret != 0) {
	errp(85, __func__, "cannot sigaction SIGHUP, errno: %d", errno);
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
	    errp(85, __func__, "cannot setitimer ITIMER_VIRTUAL, errno: %d", errno);
	    return;	// NOT REACHED
	}
    }

    /*
     * no errors detected
     */
    return;
}


/*
 * checkpoint_needed - determine if a checkpoint is needed given the Lucas sequence number
 *
 *      h               multiplier of 2
 *      n               power of 2
 *      i               Lucas sequence index
 *      multiple	if multiple > 0, checkpoint when i is a multiple
 *
 * NOTE: Unline most functions, this function does NOT perform the usual sanity check on h.
 * 	 Most functions require that h must be >= 1.  In this function, h < 1
 * 	 is treated as a condition that requires a checkpoint.
 *
 * NOTE: Unline most functions, this function does NOT perform the usual sanity check on n.
 * 	 Most functions require that n must be >= 2.  In this function n < 2
 *	 is treated as a condition that requires a checkpoint.
 *
 * NOTE: Unline most functions, this function does NOT perform the usual sanity check on i.
 * 	 Most functions require that i must be >= 2 and <= n.  In this function, i < 2 or i > n
 * 	 is treated as a condition that requires a checkpoint.
 *
 * A checkpoint is needed:
 * 	a SIGALRM signal was received
 * 	a SIGINT signal was received
 * 	when h is a bogus value
 * 	when n is a bogus value
 * 	at the start of the first Lucas sequence U(2) == v(h)
 * 	a fixed number of terms before U(n-CHECKPOINT_PREVIEW) the final Lucas term
 * 	after the end to final Lucas sequence term U(n-1) is computed
 * 	at final Lucas sequence term U(n) and primality is verified or denied
 *
 * returns:
 * 	false	no checkpoint is needed a this time
 *	true	checkpoint needed
 */
bool
checkpoint_needed(unsigned long h, unsigned long n, unsigned long i, unsigned long multiple)
{
    /*
     * signal alarms force a checkpoint
     */
    if (checkpoint_alarm != 0) {
	dbg(DBG_LOW, "checkpoint needed: checkpoint_alarm: %ld", checkpoint_alarm);
	return true;
    } else if (checkpoint_and_end != 0) {
	dbg(DBG_LOW, "checkpoint needed: checkpoint_and_end: %ld", checkpoint_and_end);
	return true;

    /*
     * firewall - unusual values of h, n and i is treated as a condition that requires a checkpoint
     */
    } else if (h < 1) {
	dbg(DBG_LOW, "checkpoint needed: h < 1: %ld", h);
	return true;
    } else if (n < 2) {
	dbg(DBG_LOW, "checkpoint needed: n < 2: %ld", n);
	return true;
    } else if (i < FIRST_TERM_INDEX) {
	dbg(DBG_LOW, "checkpoint needed: i < %d: %ld", FIRST_TERM_INDEX, i);
	return true;
    } else if (i > n) {
	dbg(DBG_LOW, "checkpoint needed: i: %ld > n: %ld", i, n);
	return true;

    /*
     * we checkpoint on the first Lucas sequence number (2)
     */
    } else if (i == FIRST_TERM_INDEX) {
	dbg(DBG_LOW, "checkpoint needed: first i: %ld == %d", i, FIRST_TERM_INDEX);
	return true;

    /*
     * we checkpoint near the end of the Lucas sequence and n is non-trivial in size
     */
    } else if (i == (n-CHECKPOINT_PREVIEW)) {
	dbg(DBG_LOW, "checkpoint needed: near end i: %ld == %ld-%d", i, n, CHECKPOINT_PREVIEW);
	return true;

    /*
     * we checkpoint on the next to last Lucas sequence
     */
    } else if (i == (n-1)) {
	dbg(DBG_LOW, "checkpoint needed: next to last i: %ld == %ld-1", i, n);
	return true;

    /*
     * we checkpoint at the end of the Lucas sequence
     */
    } else if (i == n) {
	dbg(DBG_LOW, "checkpoint needed: final i: %ld == %ld", i, n);
	return true;

    /*
     * if index is a multiple and multiple > 0
     */
    } else if ((multiple > 0) && ((i%multiple) == 0)) {
	dbg(DBG_LOW, "checkpoint needed: i: %ld multiple of: %ld", i, multiple);
	return true;
    }

    /*
     * otherwise there is no explicit need for a checkpoint
     */
    dbg(DBG_VHIGH, "no explicit need for a checkpoint at i: %ld", i);
    return false;
}


/*
 * setup_chkpt_links - setup the save and result links
 *
 *      h               multiplier of 2
 *      n               power of 2
 *      i               Lucas sequence index, or 0 --> no u term index index
 *      u_term          Lucas sequence value, or NULL --> no such value
 *
 * This function does not return on error.
 */
static void
setup_chkpt_links(unsigned long h, unsigned long n, unsigned long i, mpz_t u_term)
{
    int f_ret;		// function return value

    /*
     * If CHKPT_CUR_FILE does not exist, nothing to do, no files to link
     */
    errno = 0;
    f_ret = access(CHKPT_CUR_FILE, F_OK);
    if (f_ret != 0) {
	dbg(DBG_MED, "no current checkpoint file: %s", CHKPT_CUR_FILE);
	return;
    }

    /*
     * case: just initialized but before U(FIRST_TERM_INDEX) is calculated
     */
    if (i == 0) {
	return;

    /*
     * case: end of test - last interation
     */
    } else if (i >= n) {

    	/*
	 * we do not have a u_term to check, so we cannot test
	 */
	if (u_term == NULL) {

	    /*
	     * we were called with i > n and no u_term setop
	     */
	    dbg(DBG_LOW, "ln %s %s", CHKPT_CUR_FILE, RESULT_ERROR_FILE);
	    f_ret = link(CHKPT_CUR_FILE, RESULT_ERROR_FILE);
	    if (f_ret != 0) {
		errp(86, __func__, "ln %s %s failed, returned: %d, errno: %d", CHKPT_CUR_FILE, RESULT_ERROR_FILE, f_ret, errno);
		return;	// NOT REACHED
	    }
	    err(86, __func__, "u_term is NULL while i: %ld >= n: %ld", i, n);
	    return;	// NOT REACHED

	/*
	 * link checkpoint file to a result file
	 *
	 * h*2^n-1 is prime if and only if u_term is zero
	 */
	} else if (mpz_sgn(u_term) == 0) {

	    /*
	     * prime !!!!
	     *
	     * link CHKPT_CUR_FILE to RESULT_PRIME_FILE
	     */
	    dbg(DBG_LOW, "ln %s %s", CHKPT_CUR_FILE, RESULT_PRIME_FILE);
	    f_ret = link(CHKPT_CUR_FILE, RESULT_PRIME_FILE);
	    if (f_ret != 0) {
		errp(86, __func__, "ln %s %s failed, returned: %d, errno: %d", CHKPT_CUR_FILE, RESULT_PRIME_FILE, f_ret, errno);
		return;	// NOT REACHED
	    }

	} else {

	    /*
	     * not prime .. oh well
	     *
	     * link CHKPT_CUR_FILE to RESULT_COMPOSITE_FILE
	     */
	    dbg(DBG_LOW, "ln %s %s", CHKPT_CUR_FILE, RESULT_COMPOSITE_FILE);
	    f_ret = link(CHKPT_CUR_FILE, RESULT_COMPOSITE_FILE);
	    if (f_ret != 0) {
		errp(86, __func__, "ln %s %s failed, returned: %d, errno: %d", CHKPT_CUR_FILE, RESULT_COMPOSITE_FILE, f_ret, errno);
		return;	// NOT REACHED
	    }
	}

	/*
	 * link CHKPT_CUR_FILE to SAVE_END_FILE
	 *
	 * This marks the end of the test
	 */
	dbg(DBG_LOW, "ln %s %s", CHKPT_CUR_FILE, SAVE_END_FILE);
	f_ret = link(CHKPT_CUR_FILE, SAVE_END_FILE);
	if (f_ret != 0) {
	    errp(86, __func__, "ln %s %s failed, returned: %d, errno: %d", CHKPT_CUR_FILE, SAVE_END_FILE, f_ret, errno);
	    return;	// NOT REACHED
	}

    /*
     * case: next last interation, link the checkpoint to a save file
     */
    } else if (i == (n-1)) {

	/*
	 * nearly done .. save in case it is prime so we know if it is + or minus prime
	 *
	 * link CHKPT_CUR_FILE to SAVE_N1_FILE
	 */
	dbg(DBG_LOW, "ln %s %s", CHKPT_CUR_FILE, SAVE_N1_FILE);
	f_ret = link(CHKPT_CUR_FILE, SAVE_N1_FILE);
	if (f_ret != 0) {
	    errp(86, __func__, "ln %s %s failed, returned: %d, errno: %d", CHKPT_CUR_FILE, SAVE_N1_FILE, f_ret, errno);
	    return;	// NOT REACHED
	}

    /*
     * case: CHECKPOINT_PREVIEW within the last interation, link the checkpoint to a save file
     */
    } else if (i == (n-CHECKPOINT_PREVIEW)) {

	/*
	 * home stretch .. save in case it is prime so someone can sanity check the very end of the test
	 *
	 * link CHKPT_CUR_FILE to SAVE_NEAR_FILE
	 */
	dbg(DBG_LOW, "ln %s %s", CHKPT_CUR_FILE, SAVE_NEAR_FILE);
	f_ret = link(CHKPT_CUR_FILE, SAVE_NEAR_FILE);
	if (f_ret != 0) {
	    errp(86, __func__, "ln %s %s failed, returned: %d, errno: %d", CHKPT_CUR_FILE, SAVE_NEAR_FILE, f_ret, errno);
	    return;	// NOT REACHED
	}

    /*
     * case: first interation, link the checkpoint to a save file
     */
    } else if (i == FIRST_TERM_INDEX) {

	/*
	 * save first lucas term
	 */
	dbg(DBG_LOW, "ln %s %s", CHKPT_CUR_FILE, SAVE_FIRST_FILE);
	f_ret = link(CHKPT_CUR_FILE, SAVE_FIRST_FILE);
	if (f_ret != 0) {
	    errp(86, __func__, "ln %s %s failed, returned: %d, errno: %d", CHKPT_CUR_FILE, SAVE_FIRST_FILE, f_ret, errno);
	    return;	// NOT REACHED
	}
    }
    return;
}


/*
 * checkpoint - form a checkpoint file with the current version
 *
 * given:
 *      checkpoint_dir        directory under which checkpoint files will be created
 *      valid_test	false --> we did not test, just checkpointing for some special case
 *      h               multiplier of 2 (h must be >= 1)
 *      n               power of 2      (n must be >= 2)
 *      i               Lucas sequence index    (if valid_test is true, i must be >= 2 and <= n)
 *      				(if valid_test is false, i must be 0)
 *      v1		value of v(1) used for the given h and n (if valid_test is true, v1 must be >= 3)
 *      							 (if valid_test is false, v1 must be 0)
 *                            For Mersenne numbers, U(2) == 4.
 *                      NOTE: We assume the 1st Lucas term is U(2) = v(h).
 *      u_term          Lucas sequence value: i.e., U(i) where
 *                      U(i+1) = u(i)^2-2 mod h*2^n-1.
 *
 * This function does not return on error.
 */
void
checkpoint(const char *checkpoint_dir, bool valid_test, unsigned long h, unsigned long n, unsigned long i,
	   unsigned long v1, mpz_t u_term)
{
    FILE *stream;	// opened checkpoint file
    int f_ret;		// function return value

    /*
     * firewall
     */
    if (checkpoint_dir == NULL) {
	err(87, __func__, "checkpoint_dir is NULL");
	return;	// NOT REACHED
    }
    if (h < 1) {
	err(87, __func__, "h must be >= 1: %lu", h);
	return;	// NOT REACHED
    }
    if (n < 2) {
	err(87, __func__, "n must be >= 2: %lu", n);
	return;	// NOT REACHED
    }
    if (valid_test) {
	/* this is a valid test, so i and v1 must be valid too */
	if (i < 2) {
	    err(87, __func__, "i: %lu must be >= 2", i);
	    return;	// NOT REACHED
	}
	if (i > n) {
	    err(87, __func__, "i: %lu must be <= n: %lu", i, n);
	    return;	// NOT REACHED
	}
	if (v1 < 3) {
	    err(87, __func__, "v1: %lu must be >= 3", v1);
	    return;	// NOT REACHED
	}
    } else {
	/* this is NOT a valid test, so i and v1 must be set to 0 */
	if (i != 0) {
	    err(87, __func__, "when valid_test is false, i: %lu must be 0", i);
	    return;	// NOT REACHED
	}
	if (v1 != 0) {
	    err(87, __func__, "when valid_test is false, v1: %lu must be 0", v1);
	    return;	// NOT REACHED
	}
    }
    if (u_term == NULL) {
	err(87, __func__, "u_term is NULL");
	return;	// NOT REACHED
    }

    /*
     * If CHKPT_PREV1_FILE exists, make CHKPT_PREV1_FILE the new CHKPT_PREV2_FILE.
     */
    errno = 0;
    f_ret = access(CHKPT_PREV1_FILE, F_OK);
    if (f_ret == 0) {
	errno = 0;
	f_ret = rename(CHKPT_PREV1_FILE, CHKPT_PREV2_FILE);
	if (f_ret < 0) {
	    errp(EXIT_CHKPT_ACCESS, __func__,"cannot mv -f %s %s, errno: %d, retunded: %d",
	    			    CHKPT_PREV1_FILE, CHKPT_PREV2_FILE, errno, f_ret);
	    // exit(4);
	    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	    return;	// NOT REACHED
	}
    }

    /*
     * If CHKPT_PREV0_FILE exists, make CHKPT_PREV0_FILE the new CHKPT_PREV1_FILE.
     */
    errno = 0;
    f_ret = access(CHKPT_PREV0_FILE, F_OK);
    if (f_ret == 0) {
	errno = 0;
	f_ret = rename(CHKPT_PREV0_FILE, CHKPT_PREV1_FILE);
	if (f_ret < 0) {
	    errp(EXIT_CHKPT_ACCESS, __func__, "cannot mv -f %s %s, errno: %d, retunded: %d",
	    			    CHKPT_PREV0_FILE, CHKPT_PREV1_FILE, errno, f_ret);
	    // exit(4);
	    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	    return;	// NOT REACHED
	}
    }

    /*
     * If CHKPT_CUR_FILE exists, make CHKPT_CUR_FILE the new CHKPT_PREV0_FILE.
     */
    errno = 0;
    f_ret = access(CHKPT_CUR_FILE, F_OK);
    if (f_ret == 0) {
	errno = 0;
	f_ret = rename(CHKPT_CUR_FILE, CHKPT_PREV0_FILE);
	if (f_ret < 0) {
	    errp(EXIT_CHKPT_ACCESS, __func__, "cannot mv -f %s %s, errno: %d, retunded: %d",
	    			    CHKPT_CUR_FILE, CHKPT_PREV0_FILE, errno, f_ret);
	    // exit(4);
	    exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	    return;	// NOT REACHED
	}
    }

    /*
     * open the checkpoint file
     */
    errno = 0;
    f_ret = open(CHKPT_CUR_FILE, O_WRONLY|O_CREAT|O_EXCL, CHKPT_FILE_MODE);
    if (f_ret < 0) {
	errp(EXIT_CHKPT_ACCESS, __func__, "cannot exclusively creat for writing, errno: %d: %s", errno, CHKPT_CUR_FILE);
	// exit(4);
	exit(EXIT_CHKPT_ACCESS);	// NOT REACHED
	return;	// NOT REACHED
    }
    errno = 0;
    stream = fdopen(f_ret, "w");
    if (stream == NULL) {
	errp(87, __func__, "cannot fdopen writing, errno: %d: %s", errno, CHKPT_CUR_FILE);
	return;	// NOT REACHED
    }

    /*
     * update account stats
     */
    update_stats();

    /*
     * Version checkpoint format is always written first to the checkpoint file.
     */
    write_calc_int64_t(stream, NULL, "version", CHECKPOINT_FMT_VERSION);

    /*
     * write hostname
     */
    hostname[HOST_NAME_MAX] = '\0'; // paranoia
    write_calc_str(stream, NULL, "hostname", hostname);

    /*
     * write current working directory
     */
    cwd[HOST_NAME_MAX] = '\0'; // paranoia
    write_calc_str(stream, NULL, "cwd", cwd);
    write_calc_str(stream, NULL, "checkpoint_dir", checkpoint_dir);

    /*
     * write pid and ppid
     */
    write_calc_uint64_t(stream, NULL, "pid", pid);
    write_calc_uint64_t(stream, NULL, "ppid", ppid);

    /*
     * write n
     */
    write_calc_uint64_t(stream, NULL, "n", n);

    /*
     * write h
     */
    write_calc_uint64_t(stream, NULL, "h", h);

    /*
     * write i
     */
    write_calc_uint64_t(stream, NULL, "i", i);

    /*
     * write v(1)
     */
    write_calc_uint64_t(stream, NULL, "v1", v1);

    /*
     * write extended flag and stats
     */
    write_calc_prime_stats(stream, true);

    /*
     * write U(i) (u_term)
     */
    write_calc_mpz_hex(stream, NULL, "u_term",  u_term);

    /*
     * The string:
     *
     *	 	complete = "true" ;\n
     *
     * is always written last to the checkpoint file.
     */
    write_calc_str(stream, NULL, "complete", "true");

    /*
     * I/O buffer flush
     */
    errno = 0;
    f_ret = fflush(stream);
    if (f_ret != 0) {
	errp(87, __func__, "fflush returned: %d, errno: %d", f_ret, errno);
	return;	// NOT REACHED
    }

    /*
     * close file
     */
    errno = 0;
    f_ret = fclose(stream);
    if (f_ret != 0) {
	errp(87, __func__, "fclose returned: %d, errno: %d", f_ret, errno);
	return;	// NOT REACHED
    }

    /*
     * setup save and result links, if needed
     */
    setup_chkpt_links(h, n, i, u_term);

    /*
     * now that we have checkpointed, clear the checkpoint alarm flag if set
     */
    if (checkpoint_alarm != 0) {
	checkpoint_alarm = 0;
    }

    /*
     * now that we have checkpointed, if we saw a signal requestihg we quit, then time to exit
     */
    if (checkpoint_and_end != 0) {
	err(EXIT_SIGNAL, __func__, "caught a signal, checkpointed and gradefully exiting");
	// exit(7);
	exit(EXIT_SIGNAL);	// NOT REACHED
	return;	// NOT REACHED
    }

    /*
     * no errors detected
     */
    return;
}


/*
 * restore_checkpoint - restore state from a checkpoint directory
 *
 * given:
 *      checkpoint_dir        directory under which checkpoint files will be created
 *      h               pointer to multiplier of 2
 *      n               pointer to power of 2
 *      i               pointer to Lucas sequence index
 *      v1		pointer to value of v(1) used for the given h and n (v1 must be >= 3)
 *      u_term          pointer to Lucas sequence value
 *
 * This function does not return on error.
 */
void
restore_checkpoint(const char *checkpoint_dir, unsigned long *h, unsigned long *n, unsigned long *i,
		   unsigned long *v1, mpz_t u_term)
{
    // XXX - write this code
    err(88, __func__, "restore state from a checkpoint directory - code not yet written");
    return;	// NOT REACHED
}
