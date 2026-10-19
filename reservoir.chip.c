// HC-SR04 + model reservoir air (tangki 100 cm, sensor di atas permukaan air).
// Meniru HC-SR04: tepi turun pulsa TRIG dibalas pulsa ECHO selebar jarak × 2 ÷ 0,0343 µs.
// Mode manual  : slider "Jarak". Mode skenario: air terkuras 100 s lalu terisi 50 s (siklus 150 s simulasi).
#include "wokwi-api.h"
#include <math.h>
#include <stdlib.h>

#define TANK_CM 100.0

typedef struct {
  pin_t trig, echo;
  uint32_t distance;
  uint32_t scenario;
  timer_t t_start, t_stop;
  uint32_t echo_us;
  uint32_t rng;
} chip_state_t;

static double rnd(chip_state_t *c) {
  c->rng = c->rng * 1664525u + 1013904223u;
  return (c->rng >> 8) / 16777216.0;
}

static double scenario_distance(chip_state_t *c) {
  double t = fmod(get_sim_nanos() * 1e-9, 150.0);
  double level = t < 100.0 ? 85.0 - 70.0 * (t / 100.0)          // terkuras 85 % -> 15 %
                           : 15.0 + 70.0 * ((t - 100.0) / 50.0);  // terisi 15 % -> 85 %
  level += (rnd(c) - 0.5) * 1.0;                                     // riak permukaan air
  return TANK_CM - level;
}

static void echo_high(void *user_data) {
  chip_state_t *c = (chip_state_t *)user_data;
  pin_write(c->echo, HIGH);
  timer_start(c->t_stop, c->echo_us, false);
}

static void echo_low(void *user_data) {
  chip_state_t *c = (chip_state_t *)user_data;
  pin_write(c->echo, LOW);
}

static void on_trig(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *c = (chip_state_t *)user_data;
  if (value != LOW) return;  // pengukuran dimulai pada tepi turun TRIG
  double d = attr_read_float(c->scenario) >= 0.5 ? scenario_distance(c) : attr_read_float(c->distance);
  if (d < 2) d = 2;
  if (d > 400) d = 400;
  c->echo_us = (uint32_t)(d * 2.0 / 0.0343 + 0.5);
  timer_start(c->t_start, 200, false);  // jeda burst ultrasonik ±200 µs
}

void chip_init(void) {
  chip_state_t *c = malloc(sizeof(chip_state_t));
  c->trig = pin_init("TRIG", INPUT);
  c->echo = pin_init("ECHO", OUTPUT_LOW);
  c->distance = attr_init_float("distance", 50.0);
  c->scenario = attr_init_float("scenario", 0.0);
  c->rng = 99u;

  const timer_config_t cfg_start = { .callback = echo_high, .user_data = c };
  const timer_config_t cfg_stop = { .callback = echo_low, .user_data = c };
  c->t_start = timer_init(&cfg_start);
  c->t_stop = timer_init(&cfg_stop);

  const pin_watch_config_t watch = { .user_data = c, .edge = FALLING, .pin_change = on_trig };
  pin_watch(c->trig, &watch);
}
