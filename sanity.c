#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int
main(int argc, char *argv[]){
  int i;
  int pid;
  char input[8];
  char * pagesAlloced[32];

  printf(1,"\n\n*** Sanity : Start ***\n\n");
  printf(1, "Test 1 start: allocate max number of pages\n");
  pid = fork();
  if(pid == 0){
    // page allocation adn the read
    for (i = 0; i < 29 ; i++){
        pagesAlloced[i] = sbrk(PGSIZE);
        *pagesAlloced[i] = i;
        printf(1, "page allocation: page value: %d, address: 0x%x \n", *pagesAlloced[i], &pagesAlloced[i]);
    }
    // page reads
    for (i = 0; i < 29 ; i++){
        printf(1, "page read: page value: %d, address: 0x%x \n", *pagesAlloced[i], &pagesAlloced[i]);
    }
    printf(1,"Press Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();
  printf(1, "Test 1 end: allocate max number of pages\n");
  // ****************************** end of test 1 ******************************
printf(1, "Test 2 - start: \n");
  //allocate some pages in parent process with values in them (page i has 1000 i's written in it in sequence)
  pid = fork();
  if(pid == 0){
    int x = 0;
    for (i = 0; i < 20 ; i++){
      pagesAlloced[i] = sbrk(PGSIZE);
      for (int j = 0; j < 1000; j++){
        *(pagesAlloced[i] + j*4) = i;
      }
    }
    // notation: 0,1,...,19 are all the pages we made with sbrk
    // page reads: current pages in RAM is supposed to be => [7,...13,code_page,14,stack_page,kernel_page,15,...19]
    for (i = 0; i < 100 ; i++){ //reference string: 0 5 1 5 2 5... 19 5 ... 0 5 1 5 ...(repeats)
        x = *(pagesAlloced[i%20]);
        printf(1,"%d ",x);
        x = *(pagesAlloced[8]);
        printf(1,"%d ",x);
    }
    printf(1,"\nPress Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();

  printf(1, "Test 2 end: \n");
  // ****************************** end of test 2 ******************************
  
  // ****************************** end of test 3 ******************************

  //   printf(1, "Test start: read from father's pages \n");
  // //allocate some pages in parent process with values in them (page i has 1000 i's written in it in sequence)
  // for (i = 0; i < 5 ; i++){
  //   pagesAlloced[i] = sbrk(PGSIZE);
  //   for (int j = 0; j < 1000; j++){
  //     *(pagesAlloced[i] + j*4) = i;
  //   }
  // }
  // pid = fork();
  // if(pid == 0){
  //   // page reads
  //   for (i = 0; i < 5 ; i++){
  //       printf(1,"page values: %d %d %d %d\n",*(pagesAlloced[i] + 1*4),*(pagesAlloced[i] + 2*4),*(pagesAlloced[i] + 8*4),*(pagesAlloced[i] + 100*4));
  //   }
  //   printf(1,"Press Enter to continue\n");
  //   gets(input,8);
  //   exit();
  // }
  // wait();
  // //sbrk(-5*PGSIZE); //free the 5 allocated pages
  // printf(1, "Test end: read from father's pages \n");
  
  printf(1,"\n\n*** Sanity : End ***\n\n");
  exit();
}