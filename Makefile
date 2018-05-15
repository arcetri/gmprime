#!/bin/make
# @(#)Makefile	1.2 04 May 1995 02:06:57
#
# gmprime - gmp h*2^n-1 Risel primaloity tester
#
# @(#) $Revision$
# @(#) $Id$
# @(#) $Source$
#
# Copyright (c) 2012 by Landon Curt Noll.  All Rights Reserved.
#
# Permission to use, copy, modify, and distribute this software and
# its documentation for any purpose and without fee is hereby granted,
# provided that the above copyright, this permission notice and text
# this comment, and the disclaimer below appear in all of the following:
#
#       supporting documentation
#       source copies
#       source works derived from this source
#       binaries derived from this source or from derived source
#
# LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
# EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
# USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.
#
# chongo (Landon Curt Noll, http://www.isthe.com/chongo/index.html) /\oo/\
#
# Share and enjoy! :-)


SHELL= /bin/bash
CC= cc
CP= cp
CFLAGS= -std=c11 -Wall -Werror -pedantic -O3 -g3

TOPNAME= cmd
INSTALL= install

LUCAS= lucas.h lucas.c

OBJECTS= lucas.o

TARGETS= gmprime

all: ${TARGETS}

lucas.o: ${LUCAS}
	${CC} ${CFLAGS} -lgmp lucas.c -c
 
gmprime: gmprime.c ${OBJECTS} ${LUCAS}
	${CC} ${CFLAGS} ${OBJECTS} gmprime.c -lgmp -o $@

configure:
	@echo nothing to configure

check: gmprime test/h-n.test.txt
	cat test/h-n.test.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 0 ]]; then \
               exit 1; \
           fi; \
       done

long_check: gmprime test/h-n.small.txt
	cat test/h-n.large.txt | while read h n; do \
           if ! ./gmprime "$$h" "$$n"; then \
               exit 1; \
           fi; \
       done

longer_check: gmprime test/h-n.small.txt test/h-n.med.txt
	cat test/h-n.vlarge.txt test/h-n.huge.txt | while read h n; do \
           if ! ./gmprime "$$h" "$$n"; then \
               exit 1; \
           fi; \
       done

clobber quick_clobber:
	rm -f ${TARGETS}
	rm -rf gmprime.dSYM
	rm -f ${OBJECTS}

install: all
	@echo perhaps ${INSTALL} -m 0555 ${TARGETS} ${DESTDIR}
