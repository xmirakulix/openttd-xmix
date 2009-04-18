/* $Id$ */

/** @file math_func.cpp Math functions. */

#include "../stdafx.h"
#include "math_func.hpp"

/**
 * Compute least common multiple (lcm) of arguments \a a and \a b, the smallest
 * integer value that is a multiple of both \a a and \a b.
 * @param a First number.
 * @param b second number.
 * @return Least common multiple of values \a a and \a b.
 *
 * @note This function only works for non-negative values of \a a and \a b.
 */
int LeastCommonMultiple(int a, int b)
{
	if (a == 0 || b == 0) return 0; // By definition.
	if (a == 1 || a == b) return b;
	if (b == 1) return a;

	return a * b / GreatestCommonDivisor(a, b);
}

/**
 * Compute greatest common divisor (gcd) of \a a and \a b.
 * @param a First number.
 * @param b second number.
 * @return Greatest common divisor of \a a and \a b.
 */
int GreatestCommonDivisor(int a, int b)
{
	while (b != 0) {
		int t = b;
		b = a % b;
		a = t;
	}
	return a;

}


/**
 * Deterministic approximate division.
 * Cancels out division errors stemming from the integer nature of the division over multiple runs.
 */
int DivideApprox(int a, int b) {
	static int remainder = 0;
	static int base = 1;

	int mult = LeastCommonMultiple(base, b);
	a *= mult / b;
	remainder *= mult / base;
	base = mult;

	remainder += a % base;

	int ret = a / base;
	if (remainder >= base) {
		ret++;
		remainder -= base;
	}

	int div = GreatestCommonDivisor(remainder, base);
	remainder /= div;
	base /= div;
	return ret;
}
