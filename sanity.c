#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int
main(int argc, char *argv[]){
  int i;
    char * Fmem[32];
    char arr[3*PGSIZE];
    arr[0]='b';
    arr[2*PGSIZE+200] = 'a';
    printf(1,"%d\n",arr[2*PGSIZE+200]);

    printf(1,"\n\n***************   myMemTest : START  ***************\n\n"); 
    
    //test for access to ram 
    // for (i = 0; i < 16 - 3; i++)
    // {
    //     Fmem[i] = sbrk(PGSIZE);
    //     *Fmem[i] = i;
    //     printf(1, "page value: %d\n", *Fmem[i]);
    // }
    // test swap page
    for (i = 0; i < 33 - 3; i++)
    {
        Fmem[i] = sbrk(PGSIZE);
        *Fmem[i] = i;
        printf(1, "page valeu: %d, address: %d \n", *Fmem[i], &Fmem[i]);
    }

    // for (i = 100; i < 120; i++)
    // {
    //     *Fmem[(i % 3)] = i;
    //     printf(1, "*Fmem[%d] was accessed in tick %d\n", i % 3, uptime());
    // }


  printf(1,"done\n");
  exit();
}