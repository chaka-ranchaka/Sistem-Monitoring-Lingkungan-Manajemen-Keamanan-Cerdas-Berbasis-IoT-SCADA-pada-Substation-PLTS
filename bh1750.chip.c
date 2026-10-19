// BH1750 (GY-302) — sensor cahaya digital I2C, alamat 0x23 (ADDR ke GND).
// Menerima opcode apa pun (power on 0x01, continuous H-res 0x10, dst.), lalu saat dibaca
// mengirim 2 byte nilai mentah big-endian. Rumus datasheet: lux = raw / 1,2.
// Mode manual  : slider "lux". Mode skenario: kurva siang 4 menit + tutupan awan acak.
#include "wokwi-api.h"
#include <math.h>
#include <stdlib.h>

typedef struct {
  uint32_t lux;
  uint32_t scenario;
  uint16_t raw;
  uint8_t index;
  uint32_t rng;
  double cloud;
  double last_t;
} chip_state_t;

static double rnd(chip_state_t *c) {
  c->rng = c->rng * 1664525u + 1013904223u;
  return (c->rng >> 8) / 16777216.0;
}

static double scenario_lux(chip_state_t *c) {
  double t = get_sim_nanos() * 1e-9;
  double sun = sin(3.141592653589793 * fmod(t, 240.0) / 240.0);  // satu "hari" = 240 s simulasi
  if (sun < 0) sun = 0;
  if (t - c->last_t > 1.0) {  // tutupan awan berubah tiap ±1 s
    c->cloud += (rnd(c) - 0.5) * 0.3;
    if (c->cloud < 0.35) c->cloud = 0.35;
    if (c->cloud > 1.0) c->cloud = 1.0;
    c->last_t = t;
  }
  return 15.0 + 1100.0 * pow(sun, 1.3) * c->cloud;
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->index = 0;
  if (read) {
    double lux = attr_read_float(chip->scenario) >= 0.5 ? scenario_lux(chip) : attr_read_float(chip->lux);
    double raw = lux * 1.2;
    if (raw > 65535.0) raw = 65535.0;
    if (raw < 0.0) raw = 0.0;
    chip->raw = (uint16_t)(raw + 0.5);
  }
  return true;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  uint8_t b = (chip->index == 0) ? (chip->raw >> 8) : (chip->raw & 0xFF);
  chip->index++;
  return b;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  return true;  // opcode diterima (ACK)
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  chip->lux = attr_init_float("lux", 500.0);
  chip->scenario = attr_init_float("scenario", 0.0);
  chip->raw = 0;
  chip->index = 0;
  chip->rng = 777u;
  chip->cloud = 0.9;
  chip->last_t = 0;

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = 0x23,
    .scl = pin_init("SCL", INPUT_PULLUP),
    .sda = pin_init("SDA", INPUT_PULLUP),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
  };
  i2c_init(&i2c_config);
}
