/*
 * 测试glib的线程池机制，实现几个几个线程向一个队列写数据，一个线程池从这个队列取数据
 */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

typedef void * (*thread_func)(void *);

typedef struct varray_t {
  uint32_t size;
  int* data[0];
} varray_t;
#define VARRY_MALLOC0(len) ((varray_t *)calloc(1, sizeof(varray_t)+(len)))

static void create_thread(thread_func func, void *arg);
static void *produce_thread(void *arg);
static void consume_thread(gpointer data, gpointer user_data);

GQueue *qee;
pthread_mutex_t qee_lock;
GThreadPool *pool;



// 生产者
void producer(int num) {
  pthread_mutex_init(&qee_lock, NULL);
  qee = g_queue_new();
  static int a[10];
  for (int i=0; i<num; i++) {
    a[i] = i + 1;
    create_thread(produce_thread, &a[i]);
  }
}
// 消费者是一个线程池
void consumer(int num) {
  g_thread_init(NULL);
  pool = g_thread_pool_new(consume_thread, NULL, num, TRUE, NULL);
  if (pool == NULL) {
    printf("g_thread_pool_new error\n");
    abort();
  }
}

int main()
{
  consumer(3);
  producer(1);
  sleep(300);
  return 0;
}



static void *produce_thread(void *arg) {
  static int data[100];
  for (int i=0; i<100; i++)
    data[i] = i;
  int len = *(int*)arg;
  int cnt = 1;
  while (cnt < 100) {
    //usleep(100);
    //sleep(1);

    pthread_mutex_lock(&qee_lock);
    varray_t *varray = VARRY_MALLOC0(len);
    g_queue_push_tail(qee, varray);
    pthread_mutex_unlock(&qee_lock);

    printf("produce: %d\n", cnt);
    g_thread_pool_push(pool, &data[cnt], NULL);
    cnt++;
  }
}
static void consume_thread(gpointer data, gpointer user_data) {
  pthread_mutex_lock(&qee_lock);
  varray_t *varray = g_queue_pop_head(qee);
  pthread_mutex_unlock(&qee_lock);

  printf("                consume %lu: %d", pthread_self()%100, *(int*)data);
  printf(" size: %d, unused: %d\n", g_thread_pool_get_num_threads(pool),
         g_thread_pool_get_num_unused_threads());
  free(varray);
  usleep(10000);
}

static void create_thread(thread_func func, void *arg) {
  pthread_t thread;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  if ((ret = pthread_create(&thread, &attr, func, arg)) < 0) {
    printf("Can't create thread: %s", strerror(ret));
    abort();
  }
}