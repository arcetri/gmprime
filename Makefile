#!/bin/make
#
# gmprime - gmp h*2^n-1 Risel primaloity tester
#
# Copyright (c) 2018 by Landon Curt Noll.  All Rights Reserved.
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
#CFLAGS= -std=c11 -Wall -Werror -pedantic -O3 -g3 -DDEBUG_LINT
#CFLAGS= -std=c11 -Wall -Werror -pedantic -O3 -g3
CFLAGS= -std=c11 -Wall -pedantic -O3 -g3

DESTDIR= /usr/local/bin
INSTALL= install

SRC_C= riesel.c checkpoint.c debug.c gmprime.c
SRC_H= riesel.h checkpoint.h debug.h gmprime.h
SRC= ${SRC_C} ${SRC_H}
OBJECTS= riesel.o gmprime.o checkpoint.o debug.o

TEST_FILES= test/h-n.huge.txt test/h-n.large.txt test/h-n.med-composite.txt \
	test/h-n.med.txt test/h-n.small-composite.txt test/h-n.small.txt \
	test/h-n.test.txt test/h-n.vlarge.txt

TARGETS= gmprime

all: ${TARGETS} ${TEST_FILES}

riesel.o: riesel.c riesel.h
	${CC} ${CFLAGS} riesel.c -c

debug.o: debug.c debug.h
	${CC} ${CFLAGS} debug.c -c

checkpoint.o: checkpoint.c gmprime.h riesel.h checkpoint.h debug.h
	${CC} ${CFLAGS} checkpoint.c -c

gmprime.o: gmprime.c gmprime.h riesel.h debug.h checkpoint.h
	${CC} ${CFLAGS} gmprime.c -c

gmprime: ${OBJECTS}
	${CC} ${CFLAGS} ${OBJECTS} -lgmp -o $@

configure:
	@echo nothing to configure

# test gmprime against various parts of the verified prime table:
#
# 	https://github.com/arcetri/verified-prime
#
# For a fast test, that covers the essential cases, try:
#
# 	make check
#
# If you want to go for a bit more extensive check, try:
#
# 	make more_check
#
# Other checks will anywhere from a bit longer to very long
# depending on your hardware, compiler and gmp implementation.

check: test_check

more_check: small_check

long_check: small_check med_check

longer_check: small_check med_check large_check

even_longer_check: small_check med_check large_check vlarge_check

longest_check: small_check med_check large_check vlarge_check huge_check

# check that various non-primes are not shown to be prime
#
# For a fast test, that covers the essential cases, try:
#
# 	make small_composite_check
#
# If you want to go for a bit more extensive check, try:
#
# 	make med_composite_check
#
small_composite_check: gmprime test/h-n.small-composite.txt
	cat test/h-n.small-composite.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 1 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

med_composite_check: gmprime test/h-n.small-composite.txt test/h-n.med-composite.txt
	cat test/h-n.small-composite.txt test/h-n.med-composite.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 1 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

# checks using the individual test lists in the test sub-directory
#
# These are used by the above check.

test_check: gmprime test/h-n.test.txt
	cat test/h-n.test.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 0 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

small_check: gmprime test/h-n.small.txt
	cat test/h-n.small.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 0 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

med_check: gmprime test/h-n.med.txt
	cat test/h-n.med.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 0 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

large_check: gmprime test/h-n.large.txt
	cat test/h-n.large.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 0 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

vlarge_check: gmprime test/h-n.vlarge.txt
	cat test/h-n.vlarge.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 1 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

huge_check: gmprime test/h-n.huge.txt
	cat test/h-n.huge.txt | while read h n; do \
           ./gmprime "$$h" "$$n"; \
           status="$$?"; \
           if [[ $$status -ne 1 ]]; then \
	       echo "FATAL: test $@ for h: $$h n: $$n had unexpected exit code: $$status"; \
               exit 1; \
           fi; \
	done
	@echo "passed test: $@"

clean:
	rm -f ${OBJECTS}
	rm -rf gmprime.dSYM

clobber quick_clobber: clean
	rm -f ${TARGETS}

install: all
	${INSTALL} -m 0555 ${TARGETS} ${DESTDIR}
