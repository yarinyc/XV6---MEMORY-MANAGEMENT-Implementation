#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int i;
int pid;
char input[8];
char *pagesAlloced[32];

void test1(){
  printf(1, "Test 1 start: allocate max number of pages\n");
  pid = fork();
  if(pid == 0){
    // page allocation adn the read
    for (i = 0; i < 28 ; i++){
        pagesAlloced[i] = sbrk(PGSIZE);
        *pagesAlloced[i] = i;
        printf(1, "page allocation: page value: %d, address: 0x%x \n", *pagesAlloced[i], &pagesAlloced[i]);
    }
    // page reads
    for (i = 0; i < 28 ; i++){
        printf(1, "page read: page value: %d, address: 0x%x \n", *pagesAlloced[i], &pagesAlloced[i]);
    }
    printf(1,"Press Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();
  printf(1, "Test 1 end: allocate max number of pages\n");
}
void test2(){
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
}

void test3(){
  printf(1, "Test 3 start: paging algorithm test\n");
  pid = fork();
  if(pid == 0){
    // page allocation adn the read
    for (i = 0; i < 28 ; i++){
        pagesAlloced[i] = sbrk(PGSIZE);
        *pagesAlloced[i] = i;
    }
    // page reads
    for (i = 0; i < 1000 ; i++){
        *pagesAlloced[i%18] = 5;
        *pagesAlloced[18] = i;
        *pagesAlloced[19] = i;
        *pagesAlloced[20] = i;
        *pagesAlloced[21] = i;
        *pagesAlloced[22] = i;
        *pagesAlloced[23] = i;
        *pagesAlloced[24] = i;
        *pagesAlloced[25] = i;
        *pagesAlloced[26] = i;
        *pagesAlloced[27] = i;
    }
    //SCFIFO - 1007 PF : 1021 page out
    //NFUA - 1009 PF : 1023 page out
    //LAPA - 1008 PF : 1022 page out
    //AQ - 1008 PF : 1022 page out
    printf(1,"Press Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();
  printf(1, "Test 3 end: paging algorithm test\n");
}

void test4(){

}

int
main(int argc, char *argv[]){

  printf(1,"\n\n*** Sanity : Start ***\n\n");

  // test1();

  // test2();

  test3();

  // test4();

  printf(1, "Test start: read from father's pages \n");
  //allocate some pages in parent process with values in them (page i has 1000 i's written in it in sequence)
  for (i = 0; i < 5 ; i++){
    pagesAlloced[i] = sbrk(PGSIZE);
    for (int j = 0; j < 1000; j++){
      *(pagesAlloced[i] + j*4) = i;
    }
  }
  pid = fork();
  if(pid == 0){
    // some random offset reads for each page
    for (i = 0; i < 5 ; i++){
      printf(1,"copied page values: %d %d %d %d\n",*(pagesAlloced[i] + 1*4),*(pagesAlloced[i] + 2*4),*(pagesAlloced[i] + 8*4),*(pagesAlloced[i] + 100*4));
    }
    for (i = 0; i < 5 ; i++){
      for (int j = 0; j < 1000; j++){
        *(pagesAlloced[i] + j*4) = i*10;
      }
      printf(1,"altered copied page values: %d %d %d %d\n",*(pagesAlloced[i] + 1*4),*(pagesAlloced[i] + 2*4),*(pagesAlloced[i] + 8*4),*(pagesAlloced[i] + 100*4));
    }
    printf(1,"Press Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();
  for (i = 0; i < 5 ; i++){
      printf(1,"original page values: %d %d %d %d\n",*(pagesAlloced[i] + 1*4),*(pagesAlloced[i] + 2*4),*(pagesAlloced[i] + 8*4),*(pagesAlloced[i] + 100*4));
  }
  printf(1, "Test end: read from father's pages \n");
  

  printf(1,"\n\n*** Sanity : End ***\n\n");
  exit();
}