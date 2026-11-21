#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
#include "kernel/fs.h"
uint64_t fake_now = 0;
// the job slots are real tasks on the board; here a tiny scheduler runs them
static task_fn_t fns[40]; static int nfn;
void task_register(const char*, task_fn_t f, uint32_t){ fns[nfn++]=f; }
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_mkfs_register(); void prog_remember_register(); void prog_toggle_register();
void dev_fram_register(); void jobs_init(); void task_shell();
extern unsigned char mem[];
// a lamp-like device, and a read-and-clear "button"
static bool lamp; static bool pressed;
static int lamp_read(char *b, int n){ return snprintf(b,n,lamp?"on\n":"off\n"); }
static int lamp_write(const char *b, int n){ if(n>=2&&!strncmp(b,"on",2)){lamp=true;return n;} if(n>=3&&!strncmp(b,"off",3)){lamp=false;return n;} return -1; }
static int btn_read(char *b, int n){ bool p=pressed; pressed=false; return snprintf(b,n,p?"1\n":"0\n"); }
static const device_t dlamp={"/dev/lamp",0,0,lamp_read,lamp_write};
static const device_t dbtn={"/dev/buttons/lamp",0,0,btn_read,0};
static void run(const char *in, int ticks){ input=in; inpos=0; for(int i=0;i<ticks;i++){ task_shell(); for(int k=0;k<nfn;k++) fns[k](); fake_now+=30000; } }
int main(){
  memset(mem,0,8192);
  fs_register(&dlamp); fs_register(&dbtn);
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_ls_register(); prog_mkfs_register(); prog_remember_register(); prog_toggle_register(); jobs_init();
  run("mkfs /fram\n"
      "remember /dev/lamp /fram/lamp\n"        // foreground, file doesn't exist: saved
      "remember /dev/lamp /fram/lamp\n"        // unchanged
      "cat /fram/lamp\n"
      "remember /dev/lamp /fram/lamp &\n"
      "toggle /dev/buttons/lamp /dev/lamp &\n", 400);
  pressed=true; run("", 20);                   // a button press flips the lamp...
  run("cat /dev/lamp\n" "cat /fram/lamp\n", 200);   // ...and the file follows
  run("echo off > /dev/lamp\n" "sleep 300\n" "cat /fram/lamp\n", 400);
  run("remember /dev/nope /fram/x\n" "remember /dev/lamp\n", 200);
  // "reboot": lamp back to its default, then the boot-script order
  lamp=false; run("echo on > /dev/lamp\n", 100);  // set on, let remember save it
  run("jobs\n", 50);
  printf("\n--- reboot (lamp default off, jobs gone in reality) ---\n");
  lamp=false;
  run("cat /fram/lamp > /dev/lamp\n" "cat /dev/lamp\n", 200);
  printf("\n");
}
