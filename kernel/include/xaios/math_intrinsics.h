#ifndef XAIOS_MATH_INTRINSICS_H
#define XAIOS_MATH_INTRINSICS_H

/*
 * Freestanding math intrinsics for XAIOS kernel.
 *
 * Polynomial approximations for transcendental functions needed in
 * attention computation and rotary position embeddings.
 * No libm dependency required.
 */

float xaios_expf(float x);
float xaios_logf(float x);
float xaios_sinf(float x);
float xaios_cosf(float x);

#endif
