#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_grep_register(); void prog_wc_register();
void prog_headtail_register(); void prog_tee_register(); void prog_rm_register(); void prog_mkfs_register(); void prog_df_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[]; 
static void session(const char *in){ input=in; inpos=0;
  for(int i=0;i<4000;i++){ task_shell(); fake_now+=30000; } printf("\n"); }
int main(int argc, char**){
  memset(mem,0xff,8192);
  printf("===== first boot, blank FRAM =====\n");
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_grep_register(); prog_wc_register(); prog_headtail_register();
  prog_tee_register(); prog_rm_register(); prog_mkfs_register(); prog_df_register(); prog_ls_register(); jobs_init();
  session(
   "# a comment line\n" "ls /fram   # with a comment\n" "df\n" "echo hi > /fram/a\n"
   "mkfs /fram\n" "df\n"
   "echo hello world > /fram/a.txt\n" "cat /fram/a.txt\n"
   "echo first>/fram/log\n" "echo second >> /fram/log\n" "echo third>>/fram/log\n" "cat /fram/log\n"
   "cat /fram/log | wc -l\n" "cat /fram/log|grep sec\n"
   "ls /fram\n" "ls -l /fram/\n" "ls | tail 4\n"
   "cat /fram/nope\n" "cat < /fram/a.txt\n" "rm /fram/a.txt /fram/nope\n" "ls /fram\n"
   "echo x > /fram/waytoolongname\n"
   "script blink\n" "echo one\n" "echo two\n" ".\n" "run blink\n" "cat /fram/blink\n"
   "echo echo from-fram-boot > /fram/boot\n" "echo cat /fram/log >> /fram/boot\n" "scripts\n" "df\n");
  printf("===== reboot: RAM forgotten, FRAM kept =====\n");
  // fresh process state would be best; remount is what matters for the fs
  extern bool framfs_mount(); framfs_mount();
  session("run boot\n" "ls -l /fram\n");
}
