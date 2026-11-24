// fs/framfs.cpp retries a mount at most once a second (2026-11-24): the clock.
#include <stdint.h>
uint64_t fake_now = 0;
#include "fs/framfs.h"
#include <cstdio>
#include <cstring>
#include <stdint.h>
extern unsigned char mem[]; extern int writes_left;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static char rep[512];
static framfs_check run(const char *title){ framfs_check r; framfs_fsck(&r, rep, sizeof rep); printf("--- %s\n%s", title, rep); return r; }
static int freeb(){ int f,t; framfs_df(&f,&t); return f/64; }
static void set_chain(int b, uint16_t v){ memcpy(mem+0x240+2*b,&v,2); }
static void set_first(int i, uint16_t v){ memcpy(mem+0x40+16*i+14,&v,2); }
static void set_size(int i, uint16_t v){ memcpy(mem+0x40+16*i+12,&v,2); }
static void fresh(){ memset(mem,0,8192); framfs_mkfs(); char big[200]; memset(big,'x',200);
  framfs_write("a","hello",5,false); framfs_write("b",big,200,false); framfs_write("c",big,70,false); }
int main(){
  fresh(); framfs_check r=run("clean");
  CHECK(r.files==3 && r.used==1+4+2 && r.lost==0 && r.damaged==0);

  // 1. power cut during an overwrite: data + chain written, directory entry not
  fresh(); int before=freeb(); char nb[150]; memset(nb,'N',150);
  writes_left=3+3; framfs_write("a",nb,150,false); writes_left=-1;   // 3 data writes, 3 chain writes, then cut
  framfs_mount(); CHECK(freeb()==before-3);
  r=run("power cut during an overwrite (3 blocks leaked)");
  CHECK(r.lost==3 && r.freed==3 && r.damaged==0 && freeb()==before);
  char buf[64]; CHECK(framfs_read("a",buf,64)==5 && !memcmp(buf,"hello",5));
  framfs_mount(); CHECK(freeb()==before);   // the fix was written to the FRAM, not just the cache

  // 2. power cut during rm: directory entry cleared, chain not freed yet
  fresh(); before=freeb();
  writes_left=1; framfs_remove("b"); writes_left=-1; framfs_mount();
  r=run("power cut during rm of b (4 blocks leaked)");
  CHECK(r.files==2 && r.lost==4 && r.freed==4 && freeb()==before+4);

  // 3. cross-linked: c's chain points into b's blocks
  fresh(); framfs_mount();
  { // find first blocks from the directory
    uint16_t fb, fc; memcpy(&fb,mem+0x40+16*1+14,2); memcpy(&fc,mem+0x40+16*2+14,2);
    set_chain(fc, fb); framfs_mount(); }
  r=run("cross-linked: c runs into b's blocks");
  CHECK(r.damaged==2 && r.freed==0);   // both named: which one is wrong can't be known
  int lost_then=r.lost;
  // the user removes the damaged file, runs fsck again
  // (rm on a cross-linked file frees the shared block too: b gets damaged -- report it)
  framfs_remove("c"); framfs_mount(); r=run("after rm c: b must be intact, c's leftovers freed");
  CHECK(r.damaged==0 && r.files==2);
  char bb[256]; CHECK(framfs_read("b",bb,256)==200);
  (void)lost_then;

  // 4. a file whose chain claims a free block
  fresh(); { uint16_t fb; memcpy(&fb,mem+0x40+16*1+14,2); uint16_t nx; memcpy(&nx,mem+0x240+2*fb,2); set_chain(nx,0); framfs_mount(); }
  r=run("b's second block marked free"); CHECK(r.damaged==1 && r.freed==0);

  // 5. size larger than 1 KB, first block out of range, empty with a block
  fresh(); set_size(0, 2000); framfs_mount(); r=run("a: size 2000"); CHECK(r.damaged==1);
  fresh(); set_first(0, 200); framfs_mount(); r=run("a: first block 200"); CHECK(r.damaged==1);
  fresh(); set_size(0,0); framfs_mount(); r=run("a: size 0 but a first block"); CHECK(r.damaged==1);

  // 6. chain longer than the size says
  fresh(); set_size(1, 64); framfs_mount(); r=run("b: size says 1 block, chain has 4"); CHECK(r.damaged==1 && r.freed==0);

  // 7. duplicate name
  fresh(); memcpy(mem+0x40+16*2, "a\0\0\0\0\0\0\0\0\0\0\0", 12); framfs_mount(); r=run("two files named a"); CHECK(r.damaged==1);

  // 8. a damaged file and lost blocks together: nothing freed
  fresh(); writes_left=1; framfs_remove("b"); writes_left=-1; set_size(0,2000); framfs_mount();
  r=run("lost blocks AND a damaged file"); CHECK(r.lost==5 && r.freed==0 && r.damaged==1);   // b's 4 + a's own block

  // 9. not mounted
  memset(mem,0,8192); framfs_mount(); framfs_check r2; CHECK(framfs_fsck(&r2,rep,sizeof rep)==-1);
  printf(fails? "%d FAILURES\n":"all ok\n", fails); return fails;
}
