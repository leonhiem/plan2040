#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/task.h"
#include "kernel/fs.h"
#include "drivers/pcf8563.h"
uint64_t fake_now = 0;
static task_fn_t fns[40]; static const char *names[40]; static int nfn;
void task_register(const char *n, task_fn_t f, uint32_t){ names[nfn]=n; fns[nfn++]=f; }
static const char *input; static size_t inpos;
int getchar_timeout_us(uint32_t){ if(!input||!input[inpos]) return -1; return (unsigned char)input[inpos++]; }
// fake RTC
static rtc_time now_t; static bool rtc_ok=true;
bool rtc_get(rtc_time *t){ if(!rtc_ok) return false; *t=now_t; return true; }
bool rtc_valid(const rtc_time *t){ return t->sec<60&&t->min<60&&t->hour<24&&t->day>=1&&t->day<=31&&t->month>=1&&t->month<=12; }
int rtc_weekday_of(int y,int m,int d){ static const int k[]={0,3,2,5,0,3,5,1,4,6,2,4}; if(m<3) y--; return (y+y/4-y/100+y/400+k[m-1]+d)%7; }
void prog_cat_register(); void prog_echo_register(); void prog_ls_register(); void prog_mkfs_register();
void dev_fram_register(); void jobs_init(); void task_shell(); void dev_cron_register(); void dev_cons_register();
extern unsigned char mem[];
static int led_on=0, led_off=0, lamp_cnt=0;
static int led_write(const char *b,int n){ if(!strncmp(b,"on",2)) led_on++; else if(!strncmp(b,"off",3)) led_off++; return n; }
static int lamp_write(const char *b,int n){ lamp_cnt++; return n; }
static const device_t dled={"/dev/led",0,0,0,led_write}, dlamp={"/dev/lamp",0,0,0,lamp_write};
static void shell(const char *in, int ticks){ input=in; inpos=0; for(int i=0;i<ticks;i++){ task_shell(); fake_now+=30000; } }
static void cron_tick(){ for(int k=0;k<nfn;k++) if(!strcmp(names[k],"cron")) fns[k](); }
static void set(int y,int mo,int d,int h,int mi){ now_t={y,mo,d,0,h,mi,0,false}; }
int main(){
  memset(mem,0,8192); fs_register(&dled); fs_register(&dlamp);
  dev_fram_register(); prog_cat_register(); prog_echo_register(); prog_ls_register(); prog_mkfs_register(); dev_cons_register(); dev_cron_register(); jobs_init();
  shell("mkfs /fram\n"
    "cat > /fram/crontab\n"
    "# every minute a tick, LED on even and off odd minutes\n"
    "* * * * * echo tick > /dev/cons\n"
    "*/2 * * * * echo on > /dev/led\n"
    "1-59/2 * * * * echo off > /dev/led\n"
    "0 8 * * 1-5 echo weekday-0800 > /dev/cons\n"
    "30 9 * * 0 echo sunday-0 > /dev/cons\n"
    "30 9 * * 7 echo sunday-7 > /dev/cons\n"
    "0 12 15 * 3 echo 15th-or-wed > /dev/cons\n"
    "5,10 13 * * * echo list > /dev/cons\n"
    "0 14 * * * echo start-job > /dev/lamp &\n"
    "0 15 * * * sleep 100\n"
    "61 * * * * echo bad-minute\n"
    "* * * *\n"
    "* * * * 8 echo bad-weekday\n"
    "5/20 16 * * * echo step-from-5 > /dev/cons\n"
    ".\n", 600);
  // Sunday 2026-11-15 07:58 -> walk minutes
  int tick=0;
  auto walk=[&](int y,int mo,int d,int h,int mi,int count){ set(y,mo,d,h,mi);
    for(int i=0;i<count;i++){ printf("[%04d-%02d-%02d %02d:%02d %s] ", now_t.year,now_t.month,now_t.day,now_t.hour,now_t.min,
       (const char*[]){"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}[rtc_weekday_of(now_t.year,now_t.month,now_t.day)]); fflush(stdout);
       cron_tick(); printf("\n"); now_t.min++; if(now_t.min==60){now_t.min=0; now_t.hour++;} } };
  printf("--- boot minute is skipped, then minute by minute ---\n");
  walk(2026,11,15,7,58,4);
  printf("led on=%d off=%d (expect 1 and 2: 07:59 off, 08:00 on, 08:01 off; 07:58 skipped)\n", led_on, led_off);
  printf("--- Monday 08:00 (weekday) ---\n"); walk(2026,11,16,8,0,1);
  printf("--- Sunday 09:30 (weekday 0 and 7 both) ---\n"); walk(2026,11,15,9,30,1);
  printf("--- day-or-weekday: Sun 15th 12:00, Wed 18th 12:00, Thu 19th 12:00 ---\n");
  walk(2026,11,15,12,0,1); walk(2026,11,18,12,0,1); walk(2026,11,19,12,0,1);
  printf("--- list 13:05, 13:10, 13:07 ---\n"); walk(2026,11,19,13,5,1); walk(2026,11,19,13,10,1); walk(2026,11,19,13,7,1);
  printf("--- 14:00 starts a job, 15:00 tries a builtin ---\n"); walk(2026,11,19,14,0,1); walk(2026,11,19,15,0,1);
  shell("jobs\n",100);
  printf("--- step from 5: 16:05 16:25 16:45 vs 16:15 ---\n"); walk(2026,11,19,16,5,1); walk(2026,11,19,16,25,1); walk(2026,11,19,16,15,1);
  printf("--- VL set: nothing runs ---\n"); set(2026,11,19,17,0); now_t.vl=true; cron_tick(); now_t.min=1; cron_tick(); printf("(no output above)\n");
  printf("--- off ---\n"); now_t.vl=false; shell("echo off > /dev/cron\ncat /dev/cron\n",60); set(2026,11,19,18,0); cron_tick(); now_t.min=1; cron_tick();
  printf("(no output)\n"); shell("echo on > /dev/cron\n",60); now_t.min=2; cron_tick(); printf("\n");
  printf("--- RTC unreadable: nothing ---\n"); rtc_ok=false; now_t.min=3; cron_tick(); rtc_ok=true; printf("(no output)\n");
  shell("echo hello > /dev/cons\n",60); printf("\n");
}
