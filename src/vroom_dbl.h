#include "altrep.h"

#include "vroom_vec.h"
#include <Rcpp.h>

#include "parallel.h"

/*
    An STL iterator-based string to floating point number conversion.
    This function was adapted from the C standard library of RetroBSD,
    which is based on Berkeley UNIX.
    This function and this function only is BSD license.

    https://retrobsd.googlecode.com/svn/stable/vroom_time.h|31 col 32| for
   (const autosrc str : info->column.slice(start, end)) {/libc/stdlib/strtod.c
   */
template <class Iterator> double bsd_strtod(Iterator begin, Iterator end) {
  if (begin == end) {
    return NA_REAL;
  }
  if (*begin == 'n' || *begin == '?') {
    return NA_REAL;
  }
  int sign = 0, expSign = 0, i;
  double fraction, dblExp;
  Iterator p = begin;
  char c;

  /* Exponent read from "EX" field. */
  int exp = 0;

  /* Exponent that derives from the fractional part.  Under normal
   * circumstances, it is the negative of the number of digits in F.
   * However, if I is very long, the last digits of I get dropped
   * (otherwise a long I with a large negative exponent could cause an
   * unnecessary overflow on I alone).  In this case, fracExp is
   * incremented one for each dropped digit. */
  int fracExp = 0;

  /* Number of digits in mantissa. */
  int mantSize;

  /* Number of mantissa digits BEFORE decimal point. */
  int decPt;

  /* Temporarily holds location of exponent in str. */
  Iterator pExp;

  /* Largest possible base 10 exponent.
   * Any exponent larger than this will already
   * produce underflow or overflow, so there's
   * no need to worry about additional digits. */
  static int maxExponent = 307;

  /* Table giving binary powers of 10.
   * Entry is 10^2^i.  Used to convert decimal
   * exponents into floating-point numbers. */
  static double powersOf10[] = {
      1e1, 1e2, 1e4, 1e8, 1e16, 1e32, // 1e64, 1e128, 1e256,
  };
#if 0
        static double powersOf2[] = {
                2, 4, 16, 256, 65536, 4.294967296e9, 1.8446744073709551616e19,
                //3.4028236692093846346e38, 1.1579208923731619542e77, 1.3407807929942597099e154,
        };
        static double powersOf8[] = {
                8, 64, 4096, 2.81474976710656e14, 7.9228162514264337593e28,
                //6.2771017353866807638e57, 3.9402006196394479212e115, 1.5525180923007089351e231,
        };
        static double powersOf16[] = {
                16, 256, 65536, 1.8446744073709551616e19,
                //3.4028236692093846346e38, 1.1579208923731619542e77, 1.3407807929942597099e154,
        };
#endif

  /*
   * Strip off leading blanks and check for a sign.
   */
  p = begin;
  while (p != end && (*p == ' ' || *p == '\t'))
    ++p;
  if (p != end && *p == '-') {
    sign = 1;
    ++p;
  } else if (p != end && *p == '+')
    ++p;

  /* If we don't have a digit or decimal point something is wrong, so return an
   * NA */
  if (!(isdigit(*p) || *p == '.')) {
    return NA_REAL;
  }

  /*
   * Count the number of digits in the mantissa (including the decimal
   * point), and also locate the decimal point.
   */
  decPt = -1;
  for (mantSize = 0; p != end; ++mantSize) {
    c = *p;
    if (!isdigit(c)) {
      if (c != '.' || decPt >= 0)
        break;
      decPt = mantSize;
    }
    ++p;
  }

  /*
   * Now suck up the digits in the mantissa.  Use two integers to
   * collect 9 digits each (this is faster than using floating-point).
   * If the mantissa has more than 18 digits, ignore the extras, since
   * they can't affect the value anyway.
   */
  pExp = p;
  p -= mantSize;
  if (decPt < 0)
    decPt = mantSize;
  else
    --mantSize; /* One of the digits was the point. */

  if (mantSize > 2 * 9)
    mantSize = 2 * 9;
  fracExp = decPt - mantSize;
  if (mantSize == 0) {
    fraction = 0.0;
    p = begin;
    goto done;
  } else {
    int frac1, frac2;

    for (frac1 = 0; mantSize > 9 && p != end; --mantSize) {
      c = *p++;
      if (c == '.')
        c = *p++;
      frac1 = frac1 * 10 + (c - '0');
    }
    for (frac2 = 0; mantSize > 0 && p != end; --mantSize) {
      c = *p++;
      if (c == '.')
        c = *p++;
      frac2 = frac2 * 10 + (c - '0');
    }
    fraction = (double)1000000000 * frac1 + frac2;
  }

  /*
   * Skim off the exponent.
   */
  p = pExp;
  if (p != end &&
      (*p == 'E' || *p == 'e' || *p == 'S' || *p == 's' || *p == 'F' ||
       *p == 'f' || *p == 'D' || *p == 'd' || *p == 'L' || *p == 'l')) {
    ++p;
    if (p != end && *p == '-') {
      expSign = 1;
      ++p;
    } else if (p != end && *p == '+')
      ++p;
    while (p != end && isdigit(*p))
      exp = exp * 10 + (*p++ - '0');
  }
  if (expSign)
    exp = fracExp - exp;
  else
    exp = fracExp + exp;

  /*
   * Generate a floating-point number that represents the exponent.
   * Do this by processing the exponent one bit at a time to combine
   * many powers of 2 of 10. Then combine the exponent with the
   * fraction.
   */
  if (exp < 0) {
    expSign = 1;
    exp = -exp;
  } else
    expSign = 0;
  if (exp > maxExponent)
    exp = maxExponent;
  dblExp = 1.0;
  for (i = 0; exp; exp >>= 1, ++i)
    if (exp & 01)
      dblExp *= powersOf10[i];
  if (expSign)
    fraction /= dblExp;
  else
    fraction *= dblExp;

done:
  return sign ? -fraction : fraction;
}

Rcpp::NumericVector read_dbl(vroom_vec_info* info) {

  R_xlen_t n = info->column.size();

  Rcpp::NumericVector out(n);

  parallel_for(
      n,
      [&](size_t start, size_t end, size_t id) {
        size_t i = start;
        for (const auto& str : info->column.slice(start, end)) {
          SPDLOG_DEBUG("read_dbl(start: {} end: {} i: {})", start, end, i);
          out[i++] = bsd_strtod(str.begin(), str.end());
        }
      },
      info->num_threads,
      true);

  return out;
}

#ifdef HAS_ALTREP

/* Vroom Dbl */

class vroom_dbl : public vroom_vec {

public:
  static R_altrep_class_t class_t;

  static SEXP Make(vroom_vec_info* info) {

    SEXP out = PROTECT(R_MakeExternalPtr(info, R_NilValue, R_NilValue));
    R_RegisterCFinalizerEx(out, vroom_vec::Finalize, FALSE);

    SEXP res = R_new_altrep(class_t, out, R_NilValue);

    UNPROTECT(1);

    MARK_NOT_MUTABLE(res); /* force duplicate on modify */

    return res;
  }

  // ALTREP methods -------------------

  // What gets printed when .Internal(inspect()) is used
  static Rboolean Inspect(
      SEXP x,
      int pre,
      int deep,
      int pvec,
      void (*inspect_subtree)(SEXP, int, int, int)) {
    Rprintf(
        "vroom_dbl (len=%d, materialized=%s)\n",
        Length(x),
        R_altrep_data2(x) != R_NilValue ? "T" : "F");
    return TRUE;
  }

  // ALTREAL methods -----------------

  // the element at the index `i`
  static double real_Elt(SEXP vec, R_xlen_t i) {
    SEXP data2 = R_altrep_data2(vec);
    if (data2 != R_NilValue) {
      return REAL(data2)[i];
    }

    auto str = vroom_vec::Get(vec, i);

    return bsd_strtod(str.begin(), str.end());
  }

  // --- Altvec
  static SEXP Materialize(SEXP vec) {
    SEXP data2 = R_altrep_data2(vec);
    if (data2 != R_NilValue) {
      return data2;
    }

    auto out = read_dbl(&Info(vec));
    R_set_altrep_data2(vec, out);

    // Once we have materialized we no longer need the info
    Finalize(R_altrep_data1(vec));

    return out;
  }

  static void* Dataptr(SEXP vec, Rboolean writeable) {
    return STDVEC_DATAPTR(Materialize(vec));
  }

  // -------- initialize the altrep class with the methods above
  static void Init(DllInfo* dll) {
    vroom_dbl::class_t = R_make_altreal_class("vroom_dbl", "vroom", dll);

    // altrep
    R_set_altrep_Length_method(class_t, Length);
    R_set_altrep_Inspect_method(class_t, Inspect);

    // altvec
    R_set_altvec_Dataptr_method(class_t, Dataptr);
    R_set_altvec_Dataptr_or_null_method(class_t, Dataptr_or_null);
    R_set_altvec_Extract_subset_method(class_t, Extract_subset<vroom_dbl>);

    // altinteger
    R_set_altreal_Elt_method(class_t, real_Elt);
  }
};

R_altrep_class_t vroom_dbl::class_t;

// Called the package is loaded (needs Rcpp 0.12.18.3)
// [[Rcpp::init]]
void init_vroom_dbl(DllInfo* dll) { vroom_dbl::Init(dll); }

#else
void init_vroom_dbl(DllInfo* dll) {}
#endif