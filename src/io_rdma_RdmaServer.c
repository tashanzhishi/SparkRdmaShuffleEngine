//
// Created by wyb on 17-4-13.
//

#include "io_rdma_RdmaServer.h"
#include "jni_common.h"
#include "rdma_transport_server.h"
#include "rdma_log.h"

static JavaVM *g_jvm = NULL;

static jclass RdmaChannelHandler = NULL;
static jmethodID channelRead0 = NULL;

static jobject rdmaChannelHandler = NULL;
static jobject rdmaServer = NULL;

/*
 * Class:     io_rdma_RdmaServer
 * Method:    init
 * Signature: (Ljava/lang/String;ILio/rdma/RdmaChannelHandler;Lio/netty/buffer/PooledByteBufAllocator;)Z
 */
JNIEXPORT jboolean JNICALL Java_io_rdma_RdmaServer_init
        (JNIEnv *env, jobject obj, jstring jhost, jint jport, jobject jrch, jobject jalloc)
{
    char *host;
    int port;

    //log_init();

    host = (*env)->GetStringUTFChars(env, jhost, 0);
    port = jport;
    rdmaServer = obj;

    if (NULL == g_jvm) {
        (*env)->GetJavaVM(env, &g_jvm);
    }

    if (!RdmaChannelHandler) {
        RdmaChannelHandler = (*env)->FindClass(env, "io/rdma/RdmaChannelHandler");
        if (NULL == RdmaChannelHandler) {
            LOG(DEBUG, "find class io.rdma.RdmaChannelHandler error.\n");
            return 0;
        }
    }

    if (!channelRead0) {
        channelRead0 = (*env)->GetMethodID(env, RdmaChannelHandler, "channelRead0", "(Ljava/lang/String;[BI)V");
        if (NULL == channelRead0) {
            LOG(DEBUG, "find method channelRead0 error.\n");
            return 0;
        }
    }

    rdmaChannelHandler = (*env)->NewGlobalRef(env, jrch);

    if (-1 == init_server(host, port)) {
        return 0;
    }

    return 1;
}



/*
 * Class:     io_rdma_RdmaServer
 * Method:    destroy
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_io_rdma_RdmaServer_destroy
        (JNIEnv *env, jobject obj)
{
    LOG(DEBUG, "enter Java_io_rdma_RdmaServer_destroy.\n");
    destroy_server();
}



void jni_channel_callback(char *remote_host, jbyteArray msg, int len) {

    JNIEnv *env;
    jstring jremoteHost = NULL;

  LOG(DEBUG, "begin attach current thread");
    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
  LOG(DEBUG, " attach current thread success");
    jremoteHost = (*env)->NewStringUTF(env, remote_host);

  LOG(DEBUG, "new string utf success");
    (*env)->CallVoidMethod(env, rdmaChannelHandler, channelRead0, jremoteHost, msg, len);

   LOG(DEBUG, "call channelRead0 success %s\n", remote_host);

    (*g_jvm)->DetachCurrentThread(g_jvm);
}

jbyteArray jni_alloc_byte_array(int bytes) {

    JNIEnv *env;
    jobject jbb, jbtmp, jbbuf;
    jbyteArray jbatemp, jbaglobal;
    void *buf;

    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

    LOG(DEBUG, "jni_allocDirectBuf bytes = %d.\n", bytes);
    jbatemp = (*env)->NewByteArray(env, bytes);
    jbaglobal = (*env)->NewGlobalRef(env, jbatemp);

    (*g_jvm)->DetachCurrentThread(g_jvm);

    return jbaglobal;
}

void set_byte_array_region(jbyteArray jba, int pos, int len, unsigned char *buf) {

    JNIEnv *env;

    (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

    (*env)->SetByteArrayRegion(env, jba, pos, len, buf);

    (*g_jvm)->DetachCurrentThread(g_jvm);
}