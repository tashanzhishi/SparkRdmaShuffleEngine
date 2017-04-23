//
// Created by wyb on 17-4-13.
//

#ifndef SPARKRDMASHUFFLEENGINE_JNI_COMMON_H
#define SPARKRDMASHUFFLEENGINE_JNI_COMMON_H

#include <jni.h>

void jni_channel_callback(char *remote_host, jobject msg, int len);
jbyteArray jni_alloc_byte_array(int bytes);
void set_byte_array_region(jbyteArray jba, int pos, int len, unsigned char *buf);

#endif //SPARKRDMASHUFFLEENGINE_JNI_COMMON_H
