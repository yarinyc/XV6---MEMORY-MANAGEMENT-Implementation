#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int i;
int pid;
char input[8];
char *pagesAlloced[32];

//basic allocate and access to max number of pages
void test1(){
  printf(1, "Test 1 start: \n"); 
  pid = fork();
  if(pid == 0){
    // page allocation and read, we get error if allocate more than 28 pages (there are 4 pages in the ram)
    for (i = 0; i < 28 ; i++){
        pagesAlloced[i] = sbrk(PGSIZE);
        *pagesAlloced[i] = i;
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
  printf(1, "Test 1 end: \n");
}
// we tested here if there are just 3 page fault since we don't do swap page (cntl p)
void test2(){
  printf(1, "\nTest 2 start: \n"); 
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
    // page reads: current pages in RAM is supposed to be => [7,...13,code_page,14,stack_page,kernel_page,15,...19]
    for (i = 0; i < 100 ; i++){
        x = *(pagesAlloced[7]);
        printf(1,"%d ",x);
    }
    printf(1,"\nPress Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();
  printf(1, "Test 2 end: \n");
}
 // we wanted to check if calling every itereration to the same
 // number will lead to difference in page fault times, in different paging algorithms
void test3(){
  printf(1, "\nTest 3 start:\n");
  pid = fork();
  if(pid == 0){
    // page allocation and then read
    for (i = 0; i < 28 ; i++){
        pagesAlloced[i] = sbrk(PGSIZE);
        *pagesAlloced[i] = i;
    }
    // page reads
    printf(1,"iterations: ");
    for (i = 0; i < 100 ; i++){
      printf(1,"%d ",i);
      *pagesAlloced[i%15] = 5;
      *pagesAlloced[15] = i;
      *pagesAlloced[16] = i;
      *pagesAlloced[17] = i;
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
    // for a 1000 iterations / 1 cpu
    //SCFIFO - 14005 PF : 14019 page out
    //NFUA - 14005 PF : 14019 page out
    //LAPA - 1575 PF : 1589 page out
    //AQ - 5438 PF : 5452 page out
    printf(1,"\nPress Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();
  printf(1, "Test 3 end:\n");
}

// another check for paging algorithms.
void test4(){
printf(1, "\nTest 4 start:\n");
pid = fork();
  if(pid == 0){
    // page allocation adn the read
    for (i = 0; i < 28 ; i++){
        pagesAlloced[i] = sbrk(PGSIZE);
        *pagesAlloced[i] = i;
    }
    for (i = 0; i < 100 ; i++){
        *pagesAlloced[14] = i;
        *pagesAlloced[15] = i;
        *pagesAlloced[16] = i;
        *pagesAlloced[17] = i;
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
    printf(1,"\nPress Enter to continue\n");
    gets(input,8);
    exit();
  }
  wait();
  printf(1, "Test 4 end:\n");
}
// test cow, perform fork, then check the difference between free pages, when the child only read where
// the there is no deep copy of ram , - 68\69. then after write, diff is 89
void test5(){
  printf(1, "\nTest 5 start:\n");
  for (i = 0; i < 27 ; i++){
      pagesAlloced[i] = sbrk(PGSIZE);
      *pagesAlloced[i] = (int)i;
  }
  printf(1, "(no writes, only reads) number of free pages in the system before fork %d\n", getNumberOfFreePages());
  int pid2 = fork();
  if(pid2 == 0){
    for (i = 0; i < 100 ; i++){
      printf(1, "%d ",(int)*pagesAlloced[26-(i%6)]);
    }
    printf(1, "\n(no writes, only reads) number of free pages in the system after fork %d\n", getNumberOfFreePages());
    exit();
  }
  wait();

  printf(1, "(with writes) number of free pages in the system before fork %d\n", getNumberOfFreePages());
  int pid3 = fork();
  if(pid3 == 0){
    for (i = 0; i < 100 ; i++){
      *pagesAlloced[i%27] = i+1;
      printf(1, "%d ",(int)*pagesAlloced[i%27]);
    }
    printf(1, "\n(with writes) number of free pages in the system after fork %d\n", getNumberOfFreePages());
    exit();
  }
  wait();
  exit();
  printf(1, "Test 5 end:\n");
}


int
main(int argc, char *argv[]){

  printf(1,"\n\n*** ass3Tests : Start ***\n\n");

  test1();

  test2();

  test3();

  test4();

  //*** test 5 ***
  pid = fork();
  if(pid == 0){
    test5();
    exit();
  }
  wait();
  printf(1,"\nPress Enter to continue\n");
  gets(input,8);
  //*** test 5 ***

  //*** test 6 ***
  // check that after fork , bothe child and father have the same values, and after child changes the values,
  // the it changes only the child's values and not the father's 
  printf(1, "\nTest 6 start:\n");
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
  printf(1, "Test 6 end:\n");
  //*** test 6 ***

  printf(1,"\n\n*** Sanity : End ***\n\n");
  exit();
}