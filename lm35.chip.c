// LM35 — sensor suhu analog, keluaran 10 mV/°C (rangkaian dasar 2–150 °C).
// Mode manual  : slider "Suhu" mengatur suhu.
// Mode skenario: suhu ruang inverter berubah sendiri mengikuti waktu simulasi
//                (siklus beban 4 menit, fluktuasi kecil, kejadian overheat tiap 6 menit; waktu simulasi).
#include "wokwi-api.h"
#include <math.h>
#include <stdlib.h>

#define PI_2 6.283185307179586

typedef struct {
  pin_t vout;
  uint32_t temperature;
  uint32_t scenario;
  uint32_t rng;
  double noise;
} chip_state_t;

static double rnd(chip_state_t *c) {  // bilangan acak 0..1
  c->rng = c->rng * 1664525u + 1013904223u;
  return (c->rng >> 8) / 16777216.0;
}

static double scenario_temp(chip_state_t *c, double t) {
  double temp = 33.0 + 8.0 * sin(PI_2 * t / 240.0) + 2.0 * sin(PI_2 * t / 53.0);
  double phase = fmod(t, 360.0);
  if (phase > 280.0 && phase < 330.0) temp += 9.0 * sin(PI_2 * (phase - 280.0) / 100.0);  // overheat
  c->noise = 0.9 * c->noise + 0.1 * (rnd(c) - 0.5) * 3.0;
  return temp + c->noise;
}

static void on_timer(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  double t = get_sim_nanos() * 1e-9;
  double temp = attr_read_float(chip->scenario) >= 0.5 ? scenario_temp(chip, t)
                                                       : attr_read_float(chip->temperature);
  pin_dac_write(chip->vout, (float)(temp * 0.010));
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  chip->vout = pin_init("VOUT", ANALOG);
  chip->temperature = attr_init_float("temperature", 30.0);
  chip->scenario = attr_init_float("scenario", 0.0);
  chip->rng = 12345u;
  chip->noise = 0;

  const timer_config_t timer_config = {
    .callback = on_timer,
    .user_data = chip,
  };
  timer_t timer = timer_init(&timer_config);
  timer_start(timer, 100000, true);  // perbarui tiap 100 ms
  on_timer(chip);
}
