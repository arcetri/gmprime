# gmprime

gmprime is a software that can perform a Lucas-Lehmer-Riesel primality test for numbers
of the form __h*2<sup>n</sup>-1__.

## Motivation

The main motivations for why gmprime has been written are:

- Implement in C, the Lucas-Lehmer-Riesel test as implemented by [lucas.cal][lucas.cal].
- Implement optimized search for _V(1)_ values that satisfy the [Rödseth][rodseth] criteria.
- Implement the Lucas-Lehmer-Riesel primality test using [GNU MP][gmp] library calls.
- Allow for detailed debugging of each computational sub-step including each square, subtract 2 and modulus operations.
- Allow to check the correctness of computational sub-step using [calc][calc].

The gmprime code is open source to serve as a generic learning base for all those interested in understanding
how the Lucas-Lehmer-Riesel primality test works.
If you are trying to find very large prime numbers, such as a new largest known prime,
there currently is much better software for that purpose.
It can be used also for finding new prime numbers, but finding new prime numbers is not the purpose for
why it was written.

## Results

### Generating _V(1)_

Generating _V(1)_ is the fastest sub-step, but at the same time the most difficult to understand part
of the Lucas-Lehmer-Riesel primality test.
We used the [Rödseth][rodseth] method to generate the initial _V(1)_ value.

In general, we found the [Rödseth][rodseth] algorithm to be the most straightforward to implement and we recommend to use it,
given that it performs well in comparison with the other methods.

### Generating _U(2)_

Generating _U(2)_ = _V(h)_ requires to compute approximately log<sub>2</sub>(h) terms of the {_V<sub>i<sub>_} sequence.
Each iteration of this sub-step works by computing _V(2x+1)_ and _V(2x)_ until we reach _V(h)_.

We found out that, during every iteration, the operations of computing _V(2x+1)_ and _V(2x)_ can be easily
parallelized and do not need to be done sequentially and reduce the computation time of this sub-step by about 50%.
For simplicity, this code does not perform this parallelized optimization.
See [goprime][goprime] for an example of this type of optimization.

### Generating _U(n)_

Generating _U(n)_ is the most time consuming sub-step of the Lucas-Lehmer-Riesel primality test,
as it requires to compute _n_ terms of the
{_U<sub>i<sub>_} sequence where each term depends on the previous term (which makes it hard to parallelize).

At the time this code was written, squaring using [FLINT][flint] was FLINT is slightly faster than [GNU MP][gmp].
For this code, we chose to use [GNU MP][gmp] as that library is more commonly used.
For an example of an implementation using [FLINT][flint], see [goprime][goprime]'s C implementation.

You may wish to explore other squaring solutions. We expect that approaches based on [Crandall's transform][crandall],
George Woltman's [Gwnums library][gwnums], [Colin Percival paper][percival] or hardware-specific hand tuned code
(such as using C with inline assembly to access special hardware instructions) can achieve results at least one
order of magnitude faster than what we observed so far.

Again, the purpose of this code is NOT to be the fastest.
This code was written to help people understand how to implement the Lucas-Lehmer-Riesel primality test.

## Usage

### gmprime

```sh
# compile
#
$ make all

# perform some consistency checks
#
$ make check
$ make small_composite_check
#
# see the Makefile for an more extensive list of check rules

# Run gmprime with any h and n
#
$ ./gmprime 9448 9999
$ ./gmprime 1 23209
$ ./gmprime 391581 216193

# Run with verbose mode
#
$ ./gmprime -v 199815 163
$ ./gmprime -v 3545685 3187

# Use calc to verify correctness of the calculation
# This part requires calc to be installed and in your path
#     See https://github.com/lcn2/calc
#
$ ./gmprime -c 418791945 71 | calc -p
$ ./gmprime -c 2566851867 5634 | calc -p
```

## Future work
- Add checkpoint and restart functionality, periodically saving state and allowing for a later restart from such a state.

## Contribute

Please feel invited to contribute by creating a pull request to submit the code or bug fixes you would like to be
included in gmprime.

You can also contact us using the following email address: *gmprime-contributors (at) external (dot) cisco (dot) com*.
If you send us an email, please include the phrase "__gmprime author question__" somewhere in the subject line or
your email may be rejected.

## License

This project is distributed under the terms of the Apache License v2.0. See file "LICENSE" for further reference.

[rodseth]: <http://folk.uib.no/nmaoy/papers/luc.pdf>
[riesel]: <http://www.ams.org/journals/mcom/1969-23-108/S0025-5718-1969-0262163-1/S0025-5718-1969-0262163-1.pdf>
[penne]: <http://jpenne.free.fr/index2.html>
[flint]: <http://www.flintlib.org/>
[gmp]: <https://gmplib.org>
[big]: <https://golang.org/pkg/math/big/>
[gwnums]: <https://www.mersenne.org/download/>
[crandall]: <http://www.ams.org/journals/mcom/1994-62-205/S0025-5718-1994-1185244-1/S0025-5718-1994-1185244-1.pdf>
[percival]: <http://www.daemonology.net/papers/fft.pdf>
[calc]: <https://github.com/lcn2/calc>
[lucas.cal]: <https://github.com/lcn2/calc/blob/master/cal/lucas.cal>
[goprime]: <https://github.com/arcetri/goprime>
