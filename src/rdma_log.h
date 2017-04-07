#ifndef RDMA_LOG_H_
#define RDMA_LOG_H_

#include <stdio.h>

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
        printf(COL_RED""__FILE__"#%d: "fmt"\n"COL_END, __LINE__, ##__VA_ARGS__);     \
      } else if (level == DEBUG) {                                                   \
        printf(COL_WHITE""__FILE__"#%d: "fmt"\n"COL_END, __LINE__, ##__VA_ARGS__);   \
      } else if (level == INFO) {                                                    \
        printf(COL_GREEN""__FILE__"#%d: "fmt"\n"COL_END, __LINE__, ##__VA_ARGS__);   \
      } else {                                                                       \
        printf(COL_RED""__FILE__"#%d: LOG level must be one of {ERROR, DEBUG, INFO}\n"COL_END, __LINE__);   \
      }                                                                              \
    }                                                                                \
  } while(0)

#define GPR_ASSERT(x)                         \
  do {                                        \
    if (!(x)) {                               \
      LOG(ERROR, "assertion failed: %s", #x); \
      abort();                                \
      fflush(stdout);                         \
    }                                         \
  } while(0)

#endif /* RDMA_LOG_H_ */
