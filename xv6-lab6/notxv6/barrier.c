#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // 获得锁
  pthread_mutex_lock(&bstate.barrier_mutex); // acquire lock
  // 到达barrier的线程数量+1
  bstate.nthread++;
  if(bstate.nthread == nthread){
    // 所有线程都到达barrier
    // 轮数+1
    bstate.round++;
    // 将bstate.nthread 置零，重新开始一轮
    bstate.nthread = 0;
    // 唤醒睡在cond的所有线程
    pthread_cond_broadcast(&bstate.barrier_cond);

  }else{
    // 在cond上进入睡眠，释放锁mutex，在醒来时重新获取
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  // 释放锁
  pthread_mutex_unlock(&bstate.barrier_mutex); // release lock
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}