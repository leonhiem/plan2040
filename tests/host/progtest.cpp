#include "kernel/prog.h"
#include <cstdio>
#include <cstring>
static const program_t *reg[8]; static int nreg;
void prog_register(const program_t *p){ reg[nreg++]=p; }
void prog_wc_register(); void prog_grep_register(); void prog_headtail_register();
static const program_t *find(const char*n){ for(int i=0;i<nreg;i++) if(!strcmp(reg[i]->name,n)) return reg[i]; return 0; }
static void t(const char *prog, const char *in, int argc, const char *a0, const char *a1, const char *expect){
    char *argv[2]={(char*)a0,(char*)a1}; char out[256];
    int n=find(prog)->run(in,strlen(in),argc,argv,out,sizeof out);
    out[n<0?0:n]=0; bool ok=!strcmp(out,expect);
    printf("%s %-5s %-8s %-6s -> %s", ok?"ok  ":"FAIL", prog, a0?a0:"", a1?a1:"", out); if(!ok) printf("   expected: %s", expect);
}
int main(){ prog_wc_register(); prog_grep_register(); prog_headtail_register();
 const char *ls="/dev/led\n/dev/rtc\n/dev/rtc/vl\nbin/cat\nbin/date\n";
 t("wc",ls,0,0,0,"5 5 50\n"); t("wc",ls,1,"-l",0,"5\n"); t("wc","",0,0,0,"0 0 0\n");
 t("grep",ls,1,"rtc",0,"/dev/rtc\n/dev/rtc/vl\n"); t("grep",ls,2,"-v","/dev","bin/cat\nbin/date\n");
 t("grep","no newline",1,"new",0,"no newline\n");
 t("head",ls,1,"2",0,"/dev/led\n/dev/rtc\n"); t("head",ls,0,0,0,ls); t("head",ls,1,"0",0,"");
 t("tail",ls,1,"2",0,"bin/cat\nbin/date\n"); t("tail",ls,1,"9",0,ls); t("tail",ls,1,"0",0,"");
 t("tail","a\nb",1,"1",0,"b");
}
