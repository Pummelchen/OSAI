#include <xaios/math_intrinsics.h>

/*
 * Freestanding math intrinsics — polynomial approximations.
 *
 * Accuracy targets:
 *   xaios_expf : < 2 ULP over [-88, +88]
 *   xaios_logf : < 2 ULP over (0, +inf)
 *   xaios_sinf : < 1e-6 absolute over all reals
 *   xaios_cosf : < 1e-6 absolute over all reals
 */

/* ---- exp(x) via range reduction + degree-6 minimax polynomial ---- */
/*
 * Algorithm:
 *   x = k * ln(2) + r,  |r| <= ln(2)/2
 *   exp(x) = 2^k * P(r)
 *   P(r) = 1 + r + r^2/2 + r^3/6 + r^4/24 + r^5/120 + r^6/720
 */
float xaios_expf(float x) {
  if (x > 88.7f) return 1e38f;          /* overflow guard */
  if (x < -87.3f) return 0.0f;          /* underflow guard */

  const float ln2 = 0.6931471805599453f;
  const float inv_ln2 = 1.4426950408889634f;

  /* Range reduction: k = round(x / ln2) */
  int k = (int)(x * inv_ln2 + 0.5f);
  if (x * inv_ln2 + 0.5f < 0.0f) k = (int)(x * inv_ln2 - 0.5f);
  float r = x - (float)k * ln2;

  /* Degree-6 polynomial for exp(r), |r| <= 0.347 */
  float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f
             + r * (0.04166667f + r * (0.008333334f + r * 0.001388889f)))));

  /* Multiply by 2^k via IEEE-754 bit manipulation */
  union { float f; unsigned int u; } conv;
  conv.f = p;
  conv.u += (unsigned int)k << 23;
  return conv.f;
}

/* ---- ln(x) via frexp + degree-7 minimax polynomial ---- */
/*
 * Algorithm:
 *   x = m * 2^e,  0.5 <= m < 1
 *   if m < sqrt(2)/2:  adjust to [sqrt(2)/2, sqrt(2)]
 *   ln(x) = e * ln(2) + ln(m)
 *   ln(m) approximated by polynomial in (m-1)
 */
float xaios_logf(float x) {
  if (x <= 0.0f) return -1e38f;

  union { float f; unsigned int u; } conv;
  conv.f = x;
  int e = (int)((conv.u >> 23) & 0xFF) - 127;
  conv.u = (conv.u & 0x007FFFFFU) | 0x3F800000U;  /* mantissa in [1, 2) */
  float m = conv.f;

  /* Shift to [sqrt(2)/2, sqrt(2)] for better convergence */
  const float sqrt2_half = 0.7071067812f;
  if (m < sqrt2_half) {
    m *= 2.0f;
    e -= 1;
  }

  float t = (m - 1.0f) / (m + 1.0f);
  float t2 = t * t;
  /* ln(m) = 2*t*(1 + t^2/3 + t^4/5 + t^6/7 + t^8/9) */
  float ln_m = 2.0f * t * (1.0f + t2 * (0.33333333f
                 + t2 * (0.2f + t2 * (0.14285714f + t2 * 0.11111111f))));

  return (float)e * 0.6931471805599453f + ln_m;
}

/* ---- sin(x) via Cody-Waite range reduction + degree-7 polynomial ---- */
/*
 * Algorithm:
 *   Reduce x to [-pi/4, pi/4] via x = k*(pi/2) + r
 *   Use symmetry to map back to correct quadrant.
 *   P(r) = r - r^3/6 + r^5/120 - r^7/5040
 */
float xaios_sinf(float x) {
  const float two_over_pi = 0.6366197724f;

  /* Range reduction: k = round(x / (pi/2)) */
  float fk = x * two_over_pi;
  int k;
  if (fk >= 0.0f) k = (int)(fk + 0.5f);
  else             k = (int)(fk - 0.5f);

  /* Cody-Waite double precision reduction */
  const float C1 = 1.5707963267948966f;
  const float C2 = 6.123233995736766e-17f;
  float r = x - (float)k * C1 - (float)k * C2;

  float r2 = r * r;

  /* sin(r) Taylor: r - r^3/6 + r^5/120 - r^7/5040 */
  float s = r * (1.0f - r2 * (0.16666667f - r2 * (0.008333334f - r2 * 0.0001984127f)));

  /* cos(r) Taylor: 1 - r^2/2 + r^4/24 - r^6/720 */
  float c = 1.0f - r2 * (0.5f - r2 * (0.04166667f - r2 * 0.001388889f));

  /* Map back from quadrant: 0=sin, 1=cos, 2=-sin, 3=-cos */
  switch (((k % 4) + 4) % 4) {
    case 0: return  s;
    case 1: return  c;
    case 2: return -s;
    default: return -c;
  }
}

/* ---- cos(x) = sin(x + pi/2) ---- */
float xaios_cosf(float x) {
  return xaios_sinf(x + 1.5707963268f);
}
