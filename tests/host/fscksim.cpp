#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
#include "fs/framfs.h"
uint64_t fake_now = 0;
void task_register(const char*, task_fn_t, uint32_t){}
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_mkfs_register(); void prog_fsck_register(); void prog_df_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[]; extern int writes_left;
int main(){
  memset(mem,0,8192); framfs_mkfs();
  framfs_write("boot","date\n",5,false);
  char big[150]; memset(big,'x',150);
  writes_left=6; framfs_write("boot",big,150,false); writes_left=-1;   // power cut mid-overwrite
  printf("=== boot after the power cut ===\n");
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_ls_register(); prog_mkfs_register(); prog_fsck_register(); prog_df_register(); jobs_init();
  input = "fsck /fram\n" "cat /fram/boot\n" "fsck\n" "fsck /dev\n";
  for(int i=0;i<3000;i++){ task_shell(); fake_now+=30000; }
  printf("\n");
}
