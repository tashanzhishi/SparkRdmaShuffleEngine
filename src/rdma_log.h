#ifndef SPARKRDMASHUFFLEENGINE_RDMA_LOG_H
#define SPARKRDMASHUFFLEENGINE_RDMA_LOG_H

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>


// bash shell color
#define MY_COL(x)  "\033[;" #x "m"
#define COL_RED     MY_COL(31)
#define COL_GREEN   MY_COL(32)
#define COL_YELLOW  MY_COL(33)
#define COL_BLUE    MY_COL(34)
#define COL_MAGENTA MY_COL(35)
#define COL_CYAN    MY_COL(36)
#define COL_WHITE   MY_COL(0)
#define COL_END     "\033[;0m" /* must in the end of printf string */
// log
#define ERROR 0x01
#define DEBUG 0x02
#define INFO  0x04
#define LOG_FLAGS 0x07 //1,3,7
#define LOG(level, fmt, ...)                                                         \
  do {                                                                               \
    if (level & LOG_FLAGS) {                                                         \
      if (level == ERROR) {                                                          \
        fprintf(stderr, "[%04lu] [ERROR] %s@%s#%d: " fmt "\n", pthread_self()%10000, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__);fflush(stderr);   \
      } else if (level == DEBUG) {                                                   \
        fprintf(stderr, "[%04lu] [DEBUG] %s@%s#%d: " fmt "\n", pthread_self()%10000, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__);fflush(stderr);   \
      } else if (level == INFO) {                                                    \
        fprintf(stderr, "[%04lu] [ INFO] %s@%s#%d: " fmt "\n", pthread_self()%10000, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__);fflush(stderr);   \
      } \
    }                                                                                \
  } while(0)

#define GPR_ASSERT(x)                         \
  do {                                        \
    if (!(x)) {                               \
      LOG(ERROR, "assertion failed: %s", #x); \
      abort();                                \
      fflush(stderr);                         \
    }                                         \
  } while(0)

#endif /* SPARKRDMASHUFFLEENGINE_RDMA_LOG_H */
