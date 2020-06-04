#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int
main(int argc, char *argv[]){
  int i;
  char * pagesAlloced[32];
  int pid;
 

  printf(1,"\n\n*** Sanity : START ***\n\n");

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
    sleep(5000);
    exit();
  }
  wait();
  printf(1, "test 1 pass\n");
  
  for (i = 0; i < 5 ; i++){
    pagesAlloced[i] = sbrk(PGSIZE);
    *pagesAlloced[i] = i;
    printf(1, "pid: %d, page value: %d: \n",pid, *pagesAlloced[i]);
  }
  pid = fork();
  if(pid == 0){
    // page reads
    for (i = 0; i < 5 ; i++){
        printf(1,"pid: %d, page value: %d \n", pid, *pagesAlloced[i]);
    }
    exit();
  }
  wait();
  printf(1, "test 2 pass\n");
  
  sleep(300);




  printf(1,"done\n");
  exit();
}