// fs/framfs.cpp retries a mount at most once a second (2026-11-24): the clock.
#include <stdint.h>
uint64_t fake_now = 0;
#include "fs/framfs.h"
#include <cstdio>
#include <cstring>
extern unsigned char mem[]; extern int writes_left, write_count;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static bool is(const char *n, const char *want){ char b[1100]; int r=framfs_read(n,b,sizeof b); if(r<0) return want==0; b[r]=0; return want && !strcmp(b,want); }
static int fr(){ int f,t; framfs_df(&f,&t); return f; }
int main(){
  memset(mem,0xa5,8192);
  CHECK(!framfs_mount());                         // blank chip: no fs
  CHECK(framfs_write("a.txt","x",1,false)==-1);   // not mounted
  CHECK(framfs_mkfs()); CHECK(framfs_mount());
  int f,t; framfs_df(&f,&t); printf("df: free %d total %d\n",f,t); CHECK(t==7104 && f==7104);

  // Leon's example: a.txt 100 B, b.txt 150 B, then grow a.txt
  char a[100], b[150], more[50]; memset(a,'a',100); memset(b,'b',150); memset(more,'A',50);
  CHECK(framfs_write("a.txt",a,100,false)==100);
  CHECK(framfs_write("b.txt",b,150,false)==150);
  CHECK(framfs_write("a.txt",more,50,true)==50);
  char buf[1100]; int n=framfs_read("a.txt",buf,sizeof buf);
  CHECK(n==150 && !memcmp(buf,a,100) && !memcmp(buf+100,more,50));
  n=framfs_read("b.txt",buf,sizeof buf); CHECK(n==150 && !memcmp(buf,b,150));
  CHECK(fr()==7104-64*3-64*3);                    // a: 3 blocks, b: 3 blocks

  // persistence: forget RAM, mount again from the chip
  CHECK(framfs_mount()); CHECK(framfs_read("a.txt",buf,sizeof buf)==150);
  // overwrite shrinks, old blocks freed
  CHECK(framfs_write("a.txt","hi\n",3,false)==3); CHECK(is("a.txt","hi\n")); CHECK(fr()==7104-64-192);
  // empty file, remove, missing file
  CHECK(framfs_write("empty",0,0,false)==0); CHECK(is("empty",""));
  CHECK(framfs_remove("empty")==0); CHECK(is("empty",0)); CHECK(framfs_remove("nope")==-1);
  // names
  CHECK(framfs_write("12345678901","x",1,false)==1); CHECK(framfs_write("123456789012","x",1,false)==-1);
  CHECK(framfs_write("a/b","x",1,false)==-1); CHECK(framfs_write("a b","x",1,false)==-1); CHECK(framfs_write("","x",1,false)==-1);
  // log-style appends up to the 1 KB limit
  char line[]="2026-11-12 19:00:00 boot\n"; int total=0;
  while(framfs_write("log",line,25,true)==25) total+=25;
  printf("log grew to %d bytes before refusing\n", total); CHECK(total==1000);
  CHECK(framfs_read("log",buf,sizeof buf)==1000); CHECK(framfs_write("big",buf,1025,false)==-1);
  // listing
  char nm[16]; int sz; printf("ls:"); for(int i=0; framfs_list(i,nm,sizeof nm,&sz)==0; i++) printf(" %s(%d)",nm,sz); printf("\n");
  // fill the disk: no space -> refuse, nothing lost
  char k[1024]; memset(k,'k',1024); int files=0; char name[12];
  for(;;){ snprintf(name,12,"f%d",files); if(framfs_write(name,k,1024,false)<0) break; files++; }
  printf("filled with %d x 1KB files, free %d\n",files,fr());
  CHECK(is("a.txt","hi\n")); CHECK(framfs_read("b.txt",buf,sizeof buf)==150);
  for(int i=0;i<files;i++){ snprintf(name,12,"f%d",i); framfs_remove(name);} framfs_remove("log"); framfs_remove("12345678901");
  // directory full: 32 entries
  int made=0; for(int i=0;i<40;i++){ snprintf(name,12,"d%d",i); if(framfs_write(name,"1",1,false)==1) made++; }
  printf("directory full after %d new files (+ a.txt, b.txt)\n",made); CHECK(made==30);
  for(int i=0;i<40;i++){ snprintf(name,12,"d%d",i); framfs_remove(name); }

  // power cut at every possible write during an overwrite of b.txt:
  // after remount b.txt must be either fully old or fully new, never garbage
  char nb[200]; memset(nb,'N',200); int ok=0, tried=0;
  for(int cut=0; cut<20; cut++){
    framfs_mkfs(); framfs_write("b.txt",b,150,false);
    writes_left=cut; framfs_write("b.txt",nb,200,false); writes_left=-1;
    framfs_mount(); n=framfs_read("b.txt",buf,sizeof buf); tried++;
    if((n==150&&!memcmp(buf,b,150))||(n==200&&!memcmp(buf,nb,200))) ok++;
    else printf("  cut after %d writes: b.txt corrupt (n=%d)\n",cut,n);
  }
  printf("power-cut test: %d/%d consistent\n",ok,tried); CHECK(ok==tried);
  printf(fails? "%d FAILURES\n":"all ok\n",fails); return fails;
}
