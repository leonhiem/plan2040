#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_hexdump_register(); void prog_mkfs_register();
void dev_fram_register(); void jobs_init(); void task_shell(); void prog_wc_register();
extern unsigned char mem[];
int main(){
  memset(mem,0xff,8192);
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_hexdump_register(); prog_mkfs_register(); prog_ls_register(); prog_wc_register(); jobs_init();
  input =
   "mkfs /fram\n"
   "echo hello world > /fram/a.txt\n"
   "echo date >> /fram/boot\n"
   "hexdump /dev/fram\n"
   "hexdump -n 32 /dev/fram\n"
   "hexdump -s 0x40 -n 48 /dev/fram\n"
   "hexdump /fram/a.txt\n"
   "hexdump -s 6 -n 5 /fram/a.txt\n"
   "cat /fram/a.txt | hexdump\n"
   "hexdump -s 8190 /dev/fram\n"
   "hexdump -s 9000 /dev/fram\n"
   "hexdump /dev/nope\n"
   "hexdump -n 0 /dev/fram\n"
   "ls | hexdump | wc -l\n";
  for(int i=0;i<6000;i++){ task_shell(); fake_now+=30000; }
  // make the FRAM busy so the dump can't squeeze, to see truncation + hint with -n
  for(int i=0;i<8192;i++) mem[i]=(unsigned char)(i*37+i/7);
  input = "hexdump -s 0x1000 -n 2048 /dev/fram\n"; inpos=0;
  for(int i=0;i<2000;i++){ task_shell(); fake_now+=30000; }
  printf("\n");
}
