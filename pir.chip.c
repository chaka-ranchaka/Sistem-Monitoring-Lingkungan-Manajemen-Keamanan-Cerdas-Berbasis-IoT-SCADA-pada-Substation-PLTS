// PIR HC-SR501 — keluaran digital HIGH selama ada gerakan (jumper H, repeat trigger).
// Mode manual  : slider "Gerakan" (0 = tidak ada, 1 = ada gerakan).
// Mode skenario: orang lewat pada selang acak 20–60 s (waktu simulasi), sinyal HIGH 4–8 s per kejadian.
#include "wokwi-api.h"
#include <stdlib.h>

typedef struct {
  pin_t out;
  uint32_t motion;
  uint32_t scenario;
  double next_event, event_end;
  uint32_t rng;
} chip_state_t;

static double rnd(chip_state_t *c) {
  c->rng = c->rng * 1664525u + 1013904223u;
  return (c->rng >> 8) / 16777216.0;
}

static void on_timer(void *user_data) {
  chip_state_t *c = (chip_state_t *)user_data;
  double t = get_sim_nanos() * 1e-9;
  int high;
  if (attr_read_float(c->scenario) >= 0.5) {
    if (t >= c->next_event) {
      c->event_end = t + 4.0 + rnd(c) * 4.0;
      c->next_event = t + 20.0 + rnd(c) * 40.0;
    }
    high = t < c->event_end;
  } else {
    high = attr_read_float(c->motion) >= 0.5;
  }
  pin_write(c->out, high ? HIGH : LOW);
}

void chip_init(void) {
  chip_state_t *c = malloc(sizeof(chip_state_t));
  c->out = pin_init("OUT", OUTPUT_LOW);
  c->motion = attr_init_float("motion", 0.0);
  c->scenario = attr_init_float("scenario", 0.0);
  c->rng = 2024u;
  c->next_event = 10.0;  // kejadian pertama pada detik simulasi ke-10
  c->event_end = 0;

  const timer_config_t cfg = { .callback = on_timer, .user_data = c };
  timer_t timer = timer_init(&cfg);
  timer_start(timer, 100000, true);  // periksa tiap 100 ms
}
