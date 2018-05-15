#!/bin/bash

cat h-n.to-be-verified.txt | while read h n; do
    if [[ $n -ge 20996011 ]]; then
	echo ": ./gmprime $h $n < /dev/null"
	time ./gmprime "$h" "$n" </dev/null
	status="$?"
	if [[ $status -ne 0 ]]; then
	    echo "# composite result: ./gmprime $h $n"
	fi
    fi
done
