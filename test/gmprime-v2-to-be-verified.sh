#!/bin/bash

cat h-n.to-be-verified.txt | while read h n; do
    if [[ "$n" -ge 0 ]]; then
	echo ": ./gmprime-v2 $h $n < /dev/null"
	if ! ./gmprime-v2 "$h" "$n" < /dev/null; then
	    echo "# composite result: ./gmprime-v2 $h $n"
	fi
    fi
done
