//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"  // lab10

#define max(a, b) ((a) > (b) ? (a) : (b))   // lab10
#define min(a, b) ((a) < (b) ? (a) : (b))   // lab10

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// 系统调用 sys_mmap() 的实现，用于映射文件到进程的虚拟内存空间
uint64 sys_mmap(void) {
  uint64 addr;
  int len, prot, flags, offset;// 映射长度、权限、标志、偏移量
  struct file *f;
  struct VMA *vma = 0;// 虚拟内存区域（VMA）结构指针
  struct proc *p = myproc();// 当前进程
  int i;

  // 提取系统调用参数，并检查参数的有效性
  if (argaddr(0, &addr) < 0 || argint(1, &len) < 0
      || argint(2, &prot) < 0 || argint(3, &flags) < 0
      || argfd(4, 0, &f) < 0 || argint(5, &offset) < 0) {
    return -1;
  }
  if (flags != MAP_SHARED && flags != MAP_PRIVATE) {
    return -1;
  }
  // 如果使用了 MAP_SHARED 标志，则文件必须可写且权限包含 PROT_WRITE
  if (flags == MAP_SHARED && f->writable == 0 && (prot & PROT_WRITE)) {
    return -1;
  }
  // len 和 offset 必须为非负数，且 offset 必须是页大小的整数倍
  if (len < 0 || offset < 0 || offset % PGSIZE) {
    return -1;
  }

  // 在进程的 VMA 数组中分配一个新的 VMA 结构
  for (i = 0; i < NVMA; ++i) {
    if (!p->vma[i].addr) {
      vma = &p->vma[i];
      break;
    }
  }
  // VMA 数组已满时
  if (!vma) {
    return -1;
  }

   // 假设 addr 总是为 0，由内核选择映射的地址
  addr = MMAPMINADDR;
  for (i = 0; i < NVMA; ++i) {
    if (p->vma[i].addr) {
      // 获取当前已映射内存的最大地址 
      addr = max(addr, p->vma[i].addr + p->vma[i].len);
    }
  }
  addr = PGROUNDUP(addr);// 将地址向上对齐到页边界

  // 检查映射的地址是否会覆盖 TRAPFRAME 区域
  if (addr + len > TRAPFRAME) {
    return -1;
  }
  // 在分配的 VMA 中记录 mmap 参数
  vma->addr = addr;   
  vma->len = len;
  vma->prot = prot;
  vma->flags = flags;
  vma->offset = offset;
  vma->f = f;
  filedup(f);     // increase the file's reference count

  return addr;
}

// lab10
//将映射的部分内存进行取消映射
uint64 
sys_munmap(void) {
  uint64 addr, va;// 虚拟地址和遍历地址
  int len; // 需要解除映射的长度
  struct proc *p = myproc();
  struct VMA *vma = 0;
  uint maxsz, n, n1;
  int i;

  if (argaddr(0, &addr) < 0 || argint(1, &len) < 0) {
    return -1;
  }
  if (addr % PGSIZE || len < 0) {
    return -1;
  }

 // 查找与地址范围匹配的 VMA 结构体
  for (i = 0; i < NVMA; ++i) {
    if (p->vma[i].addr && addr >= p->vma[i].addr
        && addr + len <= p->vma[i].addr + p->vma[i].len) {
      vma = &p->vma[i];
      break;
    }
  }
  if (!vma) {
    return -1;
  }

  if (len == 0) {
    return 0;
  }

  if ((vma->flags & MAP_SHARED)) {
     // 如果映射是共享的，则需要将脏页面写回到文件中
    maxsz = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    for (va = addr; va < addr + len; va += PGSIZE) {
      if (uvmgetdirty(p->pagetable, va) == 0) {
        continue;
      }
       // 只将脏页面写回到文件
      n = min(PGSIZE, addr + len - va);
      for (i = 0; i < n; i += n1) {
        n1 = min(maxsz, n - i);
        begin_op();
        ilock(vma->f->ip);
        if (writei(vma->f->ip, 1, va + i, va - vma->addr + vma->offset + i, n1) != n1) {
          iunlock(vma->f->ip);
          end_op();
          return -1;
        }
        iunlock(vma->f->ip);
        end_op();
      }
    }
  }
  // 解除内存映射
  uvmunmap(p->pagetable, addr, (len - 1) / PGSIZE + 1, 1);
  // update the vma
  if (addr == vma->addr && len == vma->len) {
    // 如果解除的映射范围完全覆盖了 VMA，则清空 VMA
    vma->addr = 0;
    vma->len = 0;
    vma->offset = 0;
    vma->flags = 0;
    vma->prot = 0;
    fileclose(vma->f);
    vma->f = 0;
  } else if (addr == vma->addr) {
    // 如果解除的映射范围在 VMA 的开头，则更新 VMA 的起始地址和长度
    vma->addr += len;
    vma->offset += len;
    vma->len -= len;
  } else if (addr + len == vma->addr + vma->len) {
    // 如果解除的映射范围在 VMA 的结尾，则更新 VMA 的长度
    vma->len -= len;
  } else {
    panic("unexpected munmap");
  }
  return 0;
}