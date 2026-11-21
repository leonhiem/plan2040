#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
#include "kernel/fs.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_mkfs_register(); void prog_cpmv_register(); void prog_df_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[];
static int rtc_read(char *b,int n){ return snprintf(b,n,"2026-11-16 21:00:00\n"); }
static const device_t drtc={"/dev/rtc",0,0,rtc_read,0};
int main(){
  memset(mem,0,8192); fs_register(&drtc);
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_ls_register(); prog_mkfs_register(); prog_cpmv_register(); prog_df_register(); jobs_init();
  input =
   "mkfs /fram\n"
   "echo date > /fram/boot\n"
   "cp /fram/boot /fram/boot.old\n"
   "cp /dev/rtc /fram/when\n"
   "cp /dev/rtc /fram\n"
   "cp /fram/boot /fram\n"
   "cp /fram/boot /fram/boot\n"
   "cp /fram/nope /fram/x\n"
   "cp /fram/boot /fram/waytoolongname\n"
   "ls -l /fram\n"
   "cat /fram/when\n"
   "cat /fram/rtc\n"
   "echo new > /fram/boot\n"
   "mv /fram/boot.old /fram/boot\n"
   "cat /fram/boot\n"
   "mv /fram/when /fram/w2\n"
   "mv /fram/w2 /fram\n"
   "mv /fram/nope /fram/x\n"
   "mv /dev/rtc /fram/r\n"
   "mv /fram/w2 /dev/x\n"
   "mv /fram/w2 /fram/waytoolongname\n"
   "mv /fram/w2 /fram/w2\n"
   "ls -l /fram\n"
   "df\n"
   "cp\n";
  for(int i=0;i<12000;i++){ task_shell(); fake_now+=30000; }
  printf("\n");
}
