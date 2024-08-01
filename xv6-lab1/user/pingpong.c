#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
 
int 
main(int argc, char *argv[]){
    //因为实际上是双向通信，而管道只支持单向通信，所以要建立两个管道
    int parent_pipe[2];
    int child_pipe[2];
    if(pipe(parent_pipe)<0||pipe(child_pipe)<0){
        fprintf(2,"pipe error\n");
        exit(1);
    }
    //创建子进程
    int pid = fork();
    if(pid<0){
        fprintf(2,"fork error\n");
        exit(1);
    }
    char buf[64];
    if(pid == 0){ //child 
        read(parent_pipe[0], buf, 4);
        printf("%d: received %s\n", getpid(), buf);
        write(child_pipe[1], "ping", strlen("ping"));
    }else{
        // parent
        write(parent_pipe[1], "pong", strlen("pong"));
        read(child_pipe[0], buf, 4);
        printf("%d: received %s\n", getpid(), buf);
    }
    close(parent_pipe[0]);
    close(parent_pipe[1]);
    close(child_pipe[0]);
    close(child_pipe[1]);
    exit(0);
}