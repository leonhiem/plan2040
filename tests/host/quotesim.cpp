#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
uint64_t fake_now = 0;
static task_fn_t fns[40]; static int nfn;
void task_register(const char*, task_fn_t f, uint32_t){ fns[nfn++]=f; }
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_mkfs_register(); void prog_grep_register(); void prog_wc_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[];
int main(){
  memset(mem,0,8192);
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_ls_register(); prog_mkfs_register(); prog_grep_register(); prog_wc_register(); jobs_init();
  input =
   "kill 1\n"                  // the boot script's toggle job: toggle isn't in this sim
   "mkfs /fram\n"
   "echo \"a > b | c & d # e\"\n"
   "echo 'single \"double\" inside'\n"
   "echo \"it's\"\n"
   "echo a\"b c\"d\n"
   "echo \"\" x\n"
   "echo \"|\" '>' \"&\"\n"
   "echo \"*/2 * * * *     echo on > /dev/led\" >> /fram/crontab\n"
   "echo '1-59/2 * * * *  echo off > /dev/led' >> /fram/crontab\n"
   "cat /fram/crontab\n"
   "echo one two three four five six seven eight nine ten eleven twelve\n"
   "echo 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16\n"
   "echo \"unterminated\n"
   "echo 'also unterminated\n"
   "echo hello # \"not a quote problem\"\n"
   "echo \"# not a comment\"\n"
   "cat /fram/crontab | grep \"echo off\"\n"
   "cat /fram/crontab|grep 'on >'|wc -l\n"
   "echo \"x\" > \"/fram/q\"\n"
   "cat /fram/q\n"
   "\"echo\" quoted-program-name\n"
   "\"help\"\n"
   "echo \"cat > /fram/inner\" > /fram/s\n"
   "cat /fram/s\n"
   "echo 'toggle /dev/buttons/lamp /dev/lamp &' >> /fram/s\n"
   "cat /fram/s\n"
   "echo \"bg job\" > /fram/bg &\n"
   "jobs\n"
   "echo a|\n"
   "| echo a\n"
   "> /fram/x\n";
  for(int i=0;i<15000;i++){ task_shell(); for(int k=0;k<nfn;k++) fns[k](); fake_now+=30000; }
  printf("\n");
}
