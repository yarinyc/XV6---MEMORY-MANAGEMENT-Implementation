#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int
main(int argc, char *argv[]){
//   char array[4096];
//   //stack check
//   for(int i= 0; i< PGSIZE; i+=1000){
//       array[i] = 'a';
//       printf(1, "%d\n",array[i]);
//   }
  //heap check 
char * Fmem[15];
for (int i = 0; i < 15 - 3; i++)
{
    printf(1, "ghfghhfg\n");
    Fmem[i] = sbrk(PGSIZE);
    printf(1, "after\n");
    *Fmem[i] = i;
}
 
//   for(int i= 0; i< 4*PGSIZE; i+=PGSIZE){
//       *(adr + i) = i;
//   }
  printf(1,"done\n");
}