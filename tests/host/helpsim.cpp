#include <cstdio>
#include <stdint.h>
#include "kernel/task.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input="help\n"; static int inpos;
int getchar_timeout_us(uint32_t){ if(!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void jobs_init(); void task_shell();
int main(){ jobs_init(); for(int i=0;i<300;i++){ task_shell(); fake_now+=30000; } }
