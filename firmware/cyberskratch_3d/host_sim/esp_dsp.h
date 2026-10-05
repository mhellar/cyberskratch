#pragma once
inline int dsps_biquad_gen_bpf_f32(float* c, float f, float q) {
  float w0 = 2 * 3.14159265f * f, a = sinf(w0) / (2 * q), cw = cosf(w0), a0 = 1 + a;
  c[0] = a / a0; c[1] = 0; c[2] = -a / a0; c[3] = -2 * cw / a0; c[4] = (1 - a) / a0; return 0;
}
inline int dsps_biquad_f32(const float* in, float* out, int n, float* c, float* w) {
  for (int i = 0; i < n; i++) { float d0 = in[i] - c[3] * w[0] - c[4] * w[1]; out[i] = c[0] * d0 + c[1] * w[0] + c[2] * w[1]; w[1] = w[0]; w[0] = d0; } return 0;
}
