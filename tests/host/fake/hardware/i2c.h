#pragma once
// Fake: just enough for a real board.h to compile on the PC.
typedef struct i2c_inst i2c_inst_t;
extern i2c_inst_t *fake_i2c0, *fake_i2c1;
#define i2c0 fake_i2c0
#define i2c1 fake_i2c1
