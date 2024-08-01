#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXLEN 32

int main(int argc, char *argv[]) {
    char *path = "";
    char buf[MAXLEN * MAXARG] = {0}, *p;
    char *params[MAXARG];
    int paramIdx = 0;
    int i;
    int len;

    if (argc > 1) {
        if (argc + 1 > MAXARG) {//+1是考虑到后来传进的参数
            fprintf(2, "xargs: too many ars\n");
            exit(1);
        }
        path = argv[1];//为什么是argv[1]?可考虑xargs echo "hello"
        for (i = 1; i < argc; ++i) {//存储当前参数
            params[paramIdx++] = argv[i];
        }
    } else {
        fprintf(2,"xargs:short of ars");
        exit(1);
    }
    p = buf;
    while (1) {
        while (1) {//一个字符一个字符地读
            len = read(0, p, 1);
            if (len == 0 || *p == '\n') {
                break;
            }
            p++;
        }
        *p = 0;
        // 将其作为一个参数
        params[paramIdx] = buf;
        // 创建子进程执行
        if (fork() == 0) {
            exec(path, params);
            exit(0);
        } else {
            wait(0);
            p=buf;//子进程结束后，重新给p赋值
        }
        if (len == 0) {
            break;
        }
    }
    exit(0);
}
