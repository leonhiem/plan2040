#include "fs/hours.h"
#include "fs/framfs.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
extern unsigned char mem[]; extern int writes_left, write_count; extern bool read_fail;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static void reboot(){ hours_init(); }
int main(){
  // 1. Blank area (factory pattern like the real chip) -> NEW at 0, all counters
  for(int i=0;i<8192;i++) mem[i]=(i%8<4)?0x00:((i%4==0)?0xbf:(i%4==2?0xf7:0xff));
  reboot(); CHECK(hours_state(0)==HOURS_NEW); CHECK(hours_minutes(0)==0);
  for(int c=0;c<HOURS_MAX;c++) CHECK(hours_minutes(c)==0);
  reboot(); CHECK(hours_state(0)==HOURS_OK); CHECK(hours_minutes(0)==0);   // second boot: ok, not new

  // 2. Counting, persistence, never backwards
  for(uint32_t m=1;m<=500;m++) CHECK(hours_save(0,m));
  reboot(); CHECK(hours_minutes(0)==500);
  CHECK(!hours_save(0,499)); CHECK(hours_minutes(0)==500); CHECK(hours_save(0,500));
  CHECK(!hours_save(7,1)); CHECK(hours_minutes(1)==0);

  // 3. Power cut during a save, at every byte boundary we can model:
  //    the fake fails whole writes, so also simulate TORN writes below.
  int ok=0, n=0;
  for(int i=0;i<200;i++){
    uint32_t before=hours_minutes(0);
    writes_left = 0; hours_save(0, before+1); writes_left=-1;   // cut: write never happens
    reboot(); n++; if(hours_minutes(0)==before && hours_state(0)==HOURS_OK) ok++;
    CHECK(hours_save(0, before+1));
  }
  printf("cut before write: %d/%d kept old value\n", ok, n);

  // 4. Torn write: only the first k bytes of the 16-byte record land (k = 1..15)
  int torn_ok=0, torn_n=0;
  for(int k=1;k<16;k++){
    uint32_t before=hours_minutes(0);
    unsigned char snap[256]; memcpy(snap, mem+8192-256, 256);
    CHECK(hours_save(0, before+1));                 // full write happens...
    unsigned char after[256]; memcpy(after, mem+8192-256, 256);
    // ...then undo all but k bytes of the record that changed: a torn write
    int diffstart=-1; for(int i=0;i<256;i++) if(snap[i]!=after[i]){ diffstart=i; break; }
    int recstart = (diffstart-16)/16*16+16;
    memcpy(mem+8192-256+recstart+k, snap+recstart+k, 16-k);
    reboot(); torn_n++;
    uint32_t v=hours_minutes(0);
    if(hours_state(0)==HOURS_OK && (v==before || v==before+1)) torn_ok++;
    else printf("  torn at %d bytes: state %d value %u (before %u)\n", k, hours_state(0), v, before);
    hours_save(0, hours_minutes(0)+1);
  }
  printf("torn record (1..15 of 16 bytes written): %d/%d old-or-new\n", torn_ok, torn_n);

  // 5. Lost header: counters kept, header rewritten
  uint32_t keep=hours_minutes(0);
  memset(mem+8192-256, 0x55, 16); reboot();
  CHECK(hours_state(0)==HOURS_OK); CHECK(hours_minutes(0)==keep);
  reboot(); CHECK(hours_minutes(0)==keep);

  // 6. Both copies of counter 0 destroyed -> CORRUPT, not counting, not reset, others fine
  hours_save(1, 77);
  memset(mem+8192-256+16, 0x00, 32); reboot();
  CHECK(hours_state(0)==HOURS_CORRUPT); CHECK(!hours_save(0, 1)); CHECK(hours_state(0)==HOURS_CORRUPT);
  CHECK(hours_state(1)==HOURS_OK && hours_minutes(1)==77);
  reboot(); CHECK(hours_state(0)==HOURS_CORRUPT);       // stays corrupt across reboots
  // deliberate set recovers it
  CHECK(hours_set(0, keep)); CHECK(hours_state(0)==HOURS_OK);
  reboot(); CHECK(hours_state(0)==HOURS_OK && hours_minutes(0)==keep);

  // 7. Power cut during set, after the first of its two writes: new value wins
  writes_left=1; hours_set(0, 12345); writes_left=-1; reboot();
  CHECK(hours_minutes(0)==12345);
  // set lower (factory reset) works, and stays after reboot
  CHECK(hours_set(0, 0)); reboot(); CHECK(hours_minutes(0)==0);

  // 8. Read error at boot -> ERROR, NOTHING written, counters intact afterwards
  hours_set(0, 999); write_count=0; read_fail=true; reboot();
  CHECK(hours_state(0)==HOURS_ERROR); CHECK(write_count==0); CHECK(!hours_save(0,1000)); CHECK(!hours_set(0,1));
  read_fail=false; reboot(); CHECK(hours_minutes(0)==999);

  // 9. Power cut during the very first init: next boot must not lose anything
  for(int cut=0; cut<=HOURS_MAX+1; cut++){
    memset(mem+8192-256, 0xff, 256);
    writes_left=cut; reboot(); writes_left=-1;
    reboot();
    bool good = hours_state(0)==HOURS_OK || hours_state(0)==HOURS_NEW || hours_state(0)==HOURS_CORRUPT;
    CHECK(good); CHECK(hours_minutes(0)==0);
    for(int c=0;c<HOURS_MAX;c++) if(hours_state(c)==HOURS_CORRUPT){ printf("  init cut after %d writes: counter %d falsely corrupt\n",cut,c); fails++; }
    reboot(); for(int c=0;c<HOURS_MAX;c++) CHECK(hours_state(c)==HOURS_OK);
    // counters whose record never got written show as corrupt only if header was written: check no false OK values
  }

  // 10. mkfs and filesystem use never touch the system area
  hours_set(0, 4242);
  unsigned char sys[256]; memcpy(sys, mem+8192-256, 256);
  CHECK(framfs_mkfs()); CHECK(framfs_mount());
  char big[1024]; memset(big,'x',1024); char name[12]; int files=0;
  for(;;){ snprintf(name,12,"f%d",files); if(framfs_write(name,big,1024,false)<0) break; files++; }
  int fr,tot; framfs_df(&fr,&tot);
  printf("filesystem: %d bytes total, filled with %d x 1 KB files\n", tot, files);
  CHECK(tot==7104);
  CHECK(memcmp(sys, mem+8192-256, 256)==0);
  reboot(); CHECK(hours_minutes(0)==4242);

  // 11. A version-1 filesystem (old layout) must not mount
  mem[4]=1; CHECK(!framfs_mount());

  printf(fails? "%d FAILURES\n":"all ok\n", fails); return fails;
}
