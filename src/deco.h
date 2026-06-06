// Bühlmann ZHL-16C decompression engine — N2 only (air diving)
// No dynamic memory, no stdlib beyond <math.h>. Safe for FreeRTOS Core 0.
// Reference: Bühlmann A.A. "Tauchmedizin" 5th ed.; Wikipedia ZH-L16C table.
//
// Usage:
//   DecoEngine eng;
//   eng.init();                          // call once at dive start
//   eng.update(depth_m, dt_s);           // call every sample period
//   DecoResult r = eng.calculate(depth_m);  // get NDL / ceiling
#pragma once
#include <math.h>

static const int   DECO_N    = 16;
static const float DECO_FN2  = 0.7902f;  // N2 fraction in air
static const float DECO_PH2O = 0.0627f;  // water vapour at 37 °C (bar)
static const float DECO_GF   = 0.85f;    // gradient factor (recreational moderate)

// ZHL-16C N2 half-times (min), a-values (bar), b-values
static const float DECO_HT[DECO_N] = {
    5.0f,   8.0f,  12.5f,  18.5f,  27.0f,  38.3f,  54.3f,  77.0f,
  109.0f, 146.0f, 187.0f, 239.0f, 305.0f, 390.0f, 498.0f, 635.0f
};
static const float DECO_A[DECO_N] = {
  1.1696f, 1.0000f, 0.8618f, 0.7562f, 0.6200f, 0.5043f, 0.4410f, 0.4000f,
  0.3750f, 0.3500f, 0.3295f, 0.3065f, 0.2835f, 0.2610f, 0.2480f, 0.2327f
};
static const float DECO_B[DECO_N] = {
  0.5578f, 0.6514f, 0.7222f, 0.7825f, 0.8126f, 0.8434f, 0.8693f, 0.8910f,
  0.9092f, 0.9222f, 0.9319f, 0.9403f, 0.9477f, 0.9544f, 0.9602f, 0.9653f
};

struct DecoResult {
  float ceiling_m;  // minimum ascent ceiling (m), 0 if no deco required
  float ndl_min;    // no-deco limit remaining (min), 0 if already in deco
  bool  in_deco;
};

class DecoEngine {
public:
  float pN2[DECO_N];  // tissue N2 partial pressures (bar)

  // Initialise tissues to surface saturation on air
  void init() {
    float p0 = (1.0f - DECO_PH2O) * DECO_FN2;  // ~0.745 bar
    for (int i = 0; i < DECO_N; i++) pN2[i] = p0;
  }

  // Integrate Haldane gas exchange for dt_s seconds at depth_m
  void update(float depth_m, float dt_s) {
    float p_amb = 1.0f + depth_m / 10.0f;
    float p_alv = (p_amb - DECO_PH2O) * DECO_FN2;
    float dt_min = dt_s / 60.0f;
    for (int i = 0; i < DECO_N; i++) {
      float k = 0.693147f / DECO_HT[i];
      pN2[i] += (p_alv - pN2[i]) * (1.0f - expf(-k * dt_min));
    }
  }

  // Compute ceiling and NDL for current tissue state
  DecoResult calculate(float depth_m) const {
    float p_amb = 1.0f + depth_m / 10.0f;
    float p_alv = (p_amb - DECO_PH2O) * DECO_FN2;

    float ceil_bar = 0.0f;
    float ndl_min  = 999.0f;

    for (int i = 0; i < DECO_N; i++) {
      float a = DECO_A[i], b = DECO_B[i];

      // GF-adjusted ceiling pressure:  P_ceil = (pN2 - GF*a) / (1 + GF*(1/b - 1))
      float denom    = 1.0f + DECO_GF * (1.0f / b - 1.0f);
      float p_ceil_i = (pN2[i] - DECO_GF * a) / denom;
      if (p_ceil_i > ceil_bar) ceil_bar = p_ceil_i;

      // NDL: analytical time for this compartment to reach surface M-value with GF
      // M_surf_gf = 1 + GF*(a + 1/b - 1)
      float m_surf = 1.0f + DECO_GF * (a + 1.0f / b - 1.0f);
      float ndl_i  = 999.0f;

      if (pN2[i] >= m_surf) {
        ndl_i = 0.0f;  // already at or over limit
      } else if (p_alv > pN2[i]) {
        // Loading: invert Schreiner to solve for time
        float ratio = (m_surf - pN2[i]) / (p_alv - pN2[i]);
        if (ratio < 1.0f) {
          float k = 0.693147f / DECO_HT[i];
          ndl_i = -logf(1.0f - ratio) / k;
        }
        // else: saturation asymptote < m_surf → no NDL limit from this compartment
      }
      // Offgassing: compartment moves away from M-value → no NDL contribution

      if (ndl_i < ndl_min) ndl_min = ndl_i;
    }

    DecoResult r;
    r.ceiling_m = (ceil_bar > 1.0f) ? (ceil_bar - 1.0f) * 10.0f : 0.0f;
    r.in_deco   = (r.ceiling_m > 0.3f);  // >0.3 m threshold avoids floating-point noise
    r.ndl_min   = r.in_deco ? 0.0f : ndl_min;
    return r;
  }
};
