#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// struct dirent 是一个目录项结构，通常包含以下成员：

// inum：i节点号，文件系统用它来唯一标识一个文件或目录。
// name：文件名或目录名。
void
find(char *path, char *fileName) {
    char buf[128], *p;
    int fd, fd1;
    struct dirent de;
    struct stat st, st1;
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find:cannot open\n");
        return;
    }
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find:cannot stat\n");
        close(fd);
        return;
    }
    //比如find a b,一开始st指的a
    switch (st.type) {
        case T_FILE:  //是文件就错了
            fprintf(2, "path error\n");
            return; 
        case T_DIR://是文件夹才对
          // 复制当前路径，新内容附加在p指针所指位置
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/'; 
            while (read(fd, &de, sizeof(de)) == sizeof(de)) { // 遍历搜索目录
                if (de.inum == 0)
                    continue;
                //即find a .或find a ..
                if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) { 
                    continue;
                }
                memmove(p, de.name, DIRSIZ); // 不会移动指针的位置
                if ((fd1 = open(buf, 0)) >= 0) {
                    if (fstat(fd1, &st1) >= 0) {
                        switch (st1.type) {
                            case T_FILE:
                                // 若文件名与目标文件名一致，则输出其路径
                                if (!strcmp(de.name, fileName)) {
                                    printf("%s\n", buf); 
                                }
                                close(fd1); // 注意及时关闭不用的文件描述符
                                break;
                            case T_DIR:
                                close(fd1);
                                // 若为目录，则递归查找子目录
                                find(buf, fileName); 
                                break;
                        }
                    }
                }
            }
            break;
    }
    close(fd);
}
int
main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "format error\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}