//
// Created by wyb on 17-4-13.
//

#include "io_rdma_RdmaChannel.h"
#include "rdma_transport_client.h"
#include "rdma_log.h"
#include <stdlib.h>


/*
 * Class:     io_rdma_RdmaChannel
 * Method:    sendHeader
 * Signature: (Ljava/lang/String;Ljava/nio/ByteBuffer;ILio/rdma/RdmaSendCallback;)V
 */
JNIEXPORT void JNICALL Java_io_rdma_RdmaChannel_sendHeader
        (JNIEnv *env, jobject obj, jstring jhost, jint jport, jobject jmsg, jint jlen, jobject jrscb)
{
    char *host;
    unsigned char *msg = NULL;
    int len;

    jclass ByteBuffer = NULL;
    jmethodID array = NULL;
    jmethodID position = NULL;
    jbyteArray a;
    int pos;
    int port;
    int needtofree = 0;

    host = (*env)->GetStringUTFChars(env, jhost, 0);
    port = jport;

    LOG(DEBUG, "host = %s\n", host);



    msg = (*env)->GetDirectBufferAddress(env, jmsg);
    if (NULL == msg) {
        // 获取array(),position()函数标识
        ByteBuffer = (*env)->FindClass(env, "java/nio/ByteBuffer");
        if (NULL == ByteBuffer) {
            LOG(DEBUG, "find class java.nio.ByteBuffer error.\n");
            return;
        }

        array = (*env)->GetMethodID(env, ByteBuffer, "array", "()[B");
        if (NULL == array) {
            LOG(DEBUG, "find method array error.\n");
            return;
        }

        position = (*env)->GetMethodID(env, ByteBuffer, "position", "()I");
        if (NULL == position) {
            printf("find method position error.\n");
            return;
        }



        // 使用array(),position()函数获取header的byte[]对象以及pos
        a = (*env)->CallObjectMethod(env, jmsg, array);
        if (NULL == a) {
            LOG(DEBUG, "call method array error.\n");
            return;
        }

        pos = (*env)->CallIntMethod(env, jmsg, position);
        LOG(DEBUG, "position = %d.\n", pos);



        // 分配空间，并且将数据从java层拷贝到c层
        len = jlen;

        msg = malloc(len);
        needtofree = 1;
        (*env)->GetByteArrayRegion(env, a, pos, len, msg);
    }

    send_msg(host, port, msg, len);
    if (needtofree) {
        free(msg);
    }
}



/*
 * Class:     io_rdma_RdmaChannel
 * Method:    sendHeaderWithBody
 * Signature: (Ljava/lang/String;ILjava/nio/ByteBuffer;ILjava/nio/ByteBuffer;JLio/rdma/RdmaSendCallback;)V
 */
JNIEXPORT void JNICALL Java_io_rdma_RdmaChannel_sendHeaderWithBody
        (JNIEnv *env, jobject obj, jstring jhost, jint jport, jobject jheader, jint jhlen, jobject jbody, jlong jblen, jobject jrscb)
{
    char *host;
    unsigned char *header, *body;
    int hlen, blen, hpos = 0, bpos = 0, port;
    jclass ByteBuffer = NULL;
    jmethodID array = NULL;
    jmethodID position = NULL;
    jbyteArray ha, ba;
    int hneedtofree = 0, bneedtofree = 0;

    host = (*env)->GetStringUTFChars(env, jhost, 0);
    port = jport;

    hlen = jhlen;
    blen = jblen;


    // 获取array(),position()函数标识
    ByteBuffer = (*env)->FindClass(env, "java/nio/ByteBuffer");
    if (NULL == ByteBuffer) {
        LOG(DEBUG, "find class java.nio.ByteBuffer error.\n");
        return;
    }

    array = (*env)->GetMethodID(env, ByteBuffer, "array", "()[B");
    if (NULL == array) {
        LOG(DEBUG, "find method array error.\n");
        return;
    }

    position = (*env)->GetMethodID(env, ByteBuffer, "position", "()I");
    if (NULL == position) {
        printf("find method position error.\n");
        return;
    }

    header = (*env)->GetDirectBufferAddress(env, jheader);
    if (NULL == header) {
        // 使用array(),position()函数获取header的byte[]对象以及pos
        ha = (*env)->CallObjectMethod(env, jheader, array);
        if (NULL == ha) {
            LOG(DEBUG, "call method array error.\n");
            return;
        }

        hpos = (*env)->CallIntMethod(env, jheader, position);
        LOG(DEBUG, "position = %d.\n", hpos);

        header = malloc(hlen);
        hneedtofree = 1;
        (*env)->GetByteArrayRegion(env, ha, hpos, hlen, header);
    }


    body = (*env)->GetDirectBufferAddress(env, jbody);
    if (NULL == body) {
        // 使用array(),position()函数获取body的byte[]对象以及pos
        ba = (*env)->CallObjectMethod(env, jbody, array);
        if (NULL == ba) {
            LOG(DEBUG, "call method array error.\n");
            return;
        }

        bpos = (*env)->CallIntMethod(env, jbody, position);
        LOG(DEBUG, "position = %d.\n", bpos);

        body = malloc(blen);
        bneedtofree = 1;
        (*env)->GetByteArrayRegion(env, ba, bpos, blen, body);
    }


    send_msg_with_header(host, port, header, hlen, body, blen);
    if (hneedtofree) {
        free(header);
    }
    if (bneedtofree) {
        free(body);
    }
}