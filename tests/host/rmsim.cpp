#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_hexdump_register(); void prog_mkfs_register(); void prog_rm_register(); void prog_df_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[];
int main(){
  memset(mem,0,8192);
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_hexdump_register(); prog_mkfs_register(); prog_ls_register(); prog_rm_register(); prog_df_register(); jobs_init();
  input =
   "mkfs /fram\n"
   "echo hi > /fram/a\n"
   "echo the quick brown fox > /fram/fox\n" "echo jumps over the lazy >> /fram/fox\n" "echo dog, and back again, >> /fram/fox\n" "echo all day long. >> /fram/fox\n"
   "ls -l /fram\n"
   "df\n"
   "hexdump -s 0x40 -n 32 /dev/fram\n"
   "hexdump -s 0x250 -n 16 /dev/fram\n"
   "hexdump -s 0x340 -n 256 /dev/fram\n"
   "rm /fram/fox\n"
   "ls -l /fram\n"
   "df\n"
   "hexdump -s 0x40 -n 32 /dev/fram\n"
   "hexdump -s 0x250 -n 16 /dev/fram\n"
   "hexdump -s 0x340 -n 256 /dev/fram\n"
   "echo new > /fram/b\n"
   "hexdump -s 0x40 -n 32 /dev/fram\n"
   "hexdump -s 0x250 -n 16 /dev/fram\n"
   "hexdump -s 0x340 -n 256 /dev/fram\n";
  for(int i=0;i<8000;i++){ task_shell(); fake_now+=30000; }
  printf("\n");
}
