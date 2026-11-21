#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_mkfs_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[];
int main(){
  memset(mem,0,8192);
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_ls_register(); prog_mkfs_register(); jobs_init();
  input =
   "mkfs /fram\n"
   "cat > /fram/crontab\n"
   "* * * * *       echo tick > /dev/cons   # pipes & redirects kept\n"
   "*/2 * * * *     echo on > /dev/led\n"
   ".\n"
   "cat /fram/crontab\n"
   "cat >> /fram/crontab\n"
   "1-59/2 * * * *  echo off > /dev/led\n"
   ".\n"
   "cat /fram/crontab\n"
   "cat>/fram/t\n"
   "one\n"
   "two-then-ctrl-d\x04"
   "cat /fram/t\n"
   "cat >> /fram/t\n"
   "\x04"
   "cat /fram/t\n"
   "cat < /fram/t > /fram/copy\n"
   "cat /fram/copy\n"
   "echo piped | cat > /fram/p\n"
   "cat /fram/p\n"
   "cat > /fram/r\n"
   "echo from-script\n"
   "cat > /fram/inner\n"
   "echo after\n"
   ".\n"
   "run /fram/r\n"
   "ls -l /fram\n"
   "script x\n"
   "scripts\n"
   "cat > /fram/waytoolongname\n"
   "x\n"
   ".\n";
  for(int i=0;i<12000;i++){ task_shell(); fake_now+=30000; }
  printf("\n");
}
