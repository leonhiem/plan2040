#include "fs/framfs.h"
#include <cstdio>
#include <cstring>
extern unsigned char mem[]; extern int writes_left;
static bool is(const char *n, const char *want){ char b[64]; int r=framfs_read(n,b,63); if(r<0) return want==0; b[r]=0; return want && !strcmp(b,want); }
int main(){
  int ok=0, n=0;
  for(int replace=0; replace<2; replace++)
  for(int cut=0; cut<8; cut++){
    memset(mem,0,8192); framfs_mkfs();
    framfs_write("src","SOURCE",6,false);
    if(replace) framfs_write("dst","OLD",3,false);
    writes_left=cut; framfs_rename("src","dst"); writes_left=-1;
    framfs_mount(); n++;
    // the source data must survive under exactly one of the two names
    bool src=is("src","SOURCE"), dst=is("dst","SOURCE");
    bool good = (src!=dst);
    if(replace && src && !is("dst","OLD") && !is("dst",0)) good=false;  // dst may be old or gone, never garbage
    if(good) ok++; else printf("  replace=%d cut=%d: src=%d dst=%d\n",replace,cut,src,dst);
  }
  printf("power cut during mv: %d/%d keep the source data under exactly one name\n", ok, n);
}
