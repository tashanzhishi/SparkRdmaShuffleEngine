//
// Created by wyb on 17-4-13.
//

#ifndef JNI_COMMON_H
#define JNI_COMMON_H

#include <jni.h>

void jni_channel_callback(char *remote_hsot, jobject msg, int len);
void *jni_alloc_byte_array(jbyteArray *jba, int bytes);

#endif //JNI_COMMON_H
