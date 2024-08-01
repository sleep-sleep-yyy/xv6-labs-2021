  #include "kernel/types.h"
  #include "kernel/stat.h"
  #include "user/user.h"
  int main(int argc,char* argv[]){
      int pl[2];
      if(pipe(pl) < 0){
          fprintf(2,"pipe error\n");
          exit(1);
      }
      int pid=fork();
      if(pid < 0){
          fprintf(2,"fork error\n");
          exit(1);
      }
      else if(pid == 0){
          close(pl[1]);
          int prime = 0,n,pr[2];
          int first = 1;
          //循环等待pl管道的数据
          //prime用来存储进程的第一个数，即质数
          //主进程和其子进程通过pl管道通信
          while(read(pl[0],&n,1) != 0){
             //当接受来自父进程的第一个数据时，将其存储在局部变量 prime ，并打印相关信息
              if(prime == 0){
                  prime = n;
                  printf("prime %d\n",prime);
              }
              //当接受来自父进程的第二个数据时，
              else if(first){
                  first = 0;
                  //创建新的管道！！！！每个子进程和其父进程之间都有自己的pr（除了主进程）
                  pipe(pr);
                  if(fork() == 0){//子进程拥有该进程所有局部变量的副本，所以要重新初始化
                      first = 1;
                      prime = 0;
                      pl[0] = pr[0];
                      close(pr[1]);//禁掉了子进程对新管道的写，禁不了父进程对pr的写
                  }
                  else
                      close(pr[0]);
              }
              //如果不能被该进程的质数整除，则传至子进程
              if(prime != 0 && n % prime != 0){
                  write(pr[1],&n,1);
              }
          }
          close(pr[1]);
          close(pl[0]);
          wait(0);
          exit(0);
      }
      //父进程关闭了对pl的读，并依次向管道写入数据
      close(pl[0]);
      for(int i = 2 ;i <= 35 ;i++){
          write(pl[1],&i,1);//write的第二个参数是地址
      }
      close(pl[1]);
      wait(0);
      exit(0);
  }
