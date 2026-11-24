// bootsim: a firmware built with FRAM_BOOT=0 (the babywarmer) ignores
// /fram/boot at power-up and runs its built-in boot script; `run
// /fram/boot` still runs the file by hand. Compiled with -DFRAM_BOOT=0.
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
#include "fs/framfs.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[];
static void session(const char *in){ input=in; inpos=0;
  for(int i=0;i<2000;i++){ task_shell(); fake_now+=30000; } printf("\n"); }
int main(){
  memset(mem,0xff,8192);
  framfs_mkfs();
  const char *boot = "echo this is /fram/boot\n";
  framfs_write("boot", boot, strlen(boot), false);
  printf("===== power-up with a /fram/boot on the FRAM, FRAM_BOOT=0 =====\n");
  dev_fram_register(); prog_cat_register(); prog_echo_register(); jobs_init();
  session("cat /fram/boot\n" "run /fram/boot\n");
  return 0;
}
