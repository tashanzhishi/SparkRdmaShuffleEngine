#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define N 10

typedef void *(*thread_func)(void *);

struct array *a[N];
GQueue *qee;
static volatile int push_loop = 1;
static volatile int pop_loop = 1;
pthread_mutex_t lock;

extern const guint glib_major_version;
extern const guint glib_minor_version;
extern const guint glib_micro_version;

struct array {
  int size;
  int data[0];
};

void test_singlethread();
void test_multithread();

void print_array(struct array *a) {
  for (int i = 0; i < a->size; i++) {
    printf("%d ", a->data[i]);
  }
  printf("\n");
}

void print_queue(gpointer data, gpointer user_data) {
  print_array((struct array *) data);
}

void *push_thread(void *arg) {
  int i = *(int *) arg;
  int cnt = 0;
  printf("%d pid=%lu begin\n", i, pthread_self());
  abort();
  while (cnt < 100) {
    pthread_mutex_lock(&lock);
    g_queue_push_tail(qee, a[i]);
    pthread_mutex_unlock(&lock);
    usleep(10);
    cnt++;
  }
//printf("pid=%lu end %d\n", pthread_self(), cnt);
  return NULL;
}

void *pop_thread(void *arg) {
  struct array *b = NULL;
  while (pop_loop) {
    b = g_queue_pop_head(qee);
    printf("pop: ");
    print_array(b);
  }
  return NULL;
}

int main() {
  for (int i = 0; i < N; i++) {
    a[i] = (struct array *) malloc(sizeof(struct array) + (i + 1) * sizeof(int));
    a[i]->size = i + 1;
    for (int j = 0; j < i + 1; j++) {
      a[i]->data[j] = j + 1;
    }
  }
  for (int i = 0; i < N; i++) {
    print_array(a[i]);
  }
  pthread_mutex_init(&lock, NULL);

#if GLIB_MINOR_VERSION < 32
  g_thread_init(NULL);
#endif

  qee = g_queue_new();
  //test_singlethread();
  //test_multithread();
  printf("thread end\n");
  //g_queue_foreach(qee, print_queue, NULL);
  printf("%u\n", g_queue_get_length(qee));
  pthread_mutex_destroy(&lock);
  g_queue_free(qee);
  for (int i = 0; i < N; i++)
    free(a[i]);

  // glib 版本信息
  printf("glib version %d.%d.%d\n", glib_major_version, glib_minor_version, glib_micro_version);
  printf("%d.%d.%d\n", GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
  return 0;
}

void test_multithread() {
  pthread_t pid1, pid2;
  pthread_attr_t attr;
  int ret, arg1 = 1, arg2 = 2;

  pthread_attr_init(&attr);
  if ((ret = pthread_create(&pid1, &attr, push_thread, &arg1)) < 0) {
    printf("Can't create thread: %s", strerror(ret));
    abort();
  }
  if ((ret = pthread_create(&pid2, &attr, push_thread, &arg2)) < 0) {
    printf("Can't create thread: %s", strerror(ret));
    abort();
  }
  pthread_join(pid1, NULL);
  pthread_join(pid2, NULL);

}

void test_singlethread() {
  g_queue_push_tail(qee, a[0]);
  g_queue_push_tail(qee, a[1]);
  g_queue_push_tail(qee, a[2]);
  struct array *b = g_queue_peek_tail(qee);
  print_array(b);
  g_queue_foreach(qee, print_queue, NULL);

  b = g_queue_pop_head(qee);
  printf("pop: ");
  print_array(b);
  b = g_queue_pop_head(qee);
  printf("pop: ");
  print_array(b);
  g_queue_foreach(qee, print_queue, NULL);
}
