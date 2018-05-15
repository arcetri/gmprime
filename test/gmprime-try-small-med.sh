#!/bin/bash

cat h-n.small.txt h-n.med.txt | while read h n; do
    echo ": ./gmprime $h $n < /dev/null"
    if ! ./gmprime "$h" "$n" < /dev/null; then
	echo "# composite result: ./gmprime $h $n"
    fi
done
