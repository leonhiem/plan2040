#include <cstring>
#include <stdint.h>
#include "drivers/fm24cl64.h"
uint8_t mem[8192]; int writes_left = -1; int write_count = 0;   // -1 = no power cut
bool fram_read(uint16_t a, uint8_t *b, int n){ if(a+n>8192) return false; memcpy(b,mem+a,n); return true; }
bool fram_write(uint16_t a, const uint8_t *b, int n){
  if(a+n>8192) return false;
  if(writes_left==0) return false;
  if(writes_left>0) writes_left--;
  write_count++; memcpy(mem+a,b,n); return true; }
