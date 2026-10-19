// SCT-013-000 (100 A : 50 mA, rasio 2000:1) + resistor burden 22 Ω + bias 1,65 V.
// Wokwi tidak menyimulasikan rangkaian resistor, jadi seluruh pengkondisian sinyal
// dimodelkan di sini. Pin OUT = 1,65 V + (I_rms × √2 ÷ 2000 × 22) × sin(2π·50·t).
// Mode manual  : slider "Arus". Mode skenario: beban berubah per 2 menit + lonjakan arus tiap 6 menit.
#include "wokwi-api.h"
#include <math.h>
#include <stdlib.h>

#define CT_RATIO   2000.0
#define R_BURDEN   22.0
#define V_BIAS     1.65
#define F_GRID     50.0
#define PI_2       6.283185307179586

typedef struct {
  pin_t out;
  uint32_t current;
  uint32_t scenario;
  double i_rms;
  double last_t;
  uint32_t rng;
} chip_state_t;

static double rnd(chip_state_t *c) {
  c->rng = c->rng * 1664525u + 1013904223u;
  return (c->rng >> 8) / 16777216.0;
}

static void update_scenario(chip_state_t *c, double t) {
  if (t - c->last_t < 0.5) return;  // nilai RMS skenario diperbarui tiap 0,5 s
  c->last_t = t;
  double i = 0.7 + 0.45 * sin(PI_2 * t / 120.0) + (rnd(c) - 0.5) * 0.1;
  double phase = fmod(t, 360.0);
  if (phase > 160.0 && phase < 180.0) i = 2.6 + (rnd(c) - 0.5) * 0.2;  // lonjakan arus
  c->i_rms = i < 0 ? 0 : i;
}

static void on_timer(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  double t = get_sim_nanos() * 1e-9;
  double irms;
  if (attr_read_float(chip->scenario) >= 0.5) {
    update_scenario(chip, t);
    irms = chip->i_rms;
  } else {
    irms = attr_read_float(chip->current);
  }
  double i_peak = irms * sqrt(2.0);
  double v = V_BIAS + (i_peak / CT_RATIO) * R_BURDEN * sin(PI_2 * F_GRID * t);
  pin_dac_write(chip->out, (float)v);
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  chip->out = pin_init("OUT", ANALOG);
  chip->current = attr_init_float("current", 0.5);
  chip->scenario = attr_init_float("scenario", 0.0);
  chip->i_rms = 0.7;
  chip->last_t = -1;
  chip->rng = 4242u;

  const timer_config_t timer_config = {
    .callback = on_timer,
    .user_data = chip,
  };
  timer_t timer = timer_init(&timer_config);
  timer_start(timer, 200, true);  // 100 titik per siklus 50 Hz
  on_timer(chip);
}
