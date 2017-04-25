//
// Created by wyb on 17-4-23.
//

#include "rdma_transport.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "thread.h"
#include "jni_common.h"

extern struct rdma_context *g_rdma_context;

static void *poll_thread(void *arg);
static void *handle_thread(void *arg);
static void handle_send_event(struct ibv_wc *wc);
static void handle_recv_event(struct ibv_wc *wc);

/*struct ibv_wc_short {
  uint64_t		wr_id;
  enum ibv_wc_status	status;
  enum ibv_wc_opcode	opcode;
  uint32_t		byte_len;
};*/


int rdma_create_connect(struct rdma_transport *transport) {
  LOG(DEBUG, "rdma_create_connect begin");
  GPR_ASSERT(transport);

  transport->work_queue = g_queue_new();
  transport->is_work_running = 0;
  pthread_mutex_init(&transport->work_queue_lock, NULL);

  transport->handle_quque = g_queue_new();
  transport->is_handle_running = 0;
  pthread_mutex_init(&transport->handle_queue_lock, NULL);
  pthread_cond_init(&transport->handle_cv, NULL);

  transport->comp_channel = ibv_create_comp_channel(g_rdma_context->context);
  GPR_ASSERT(transport->comp_channel);
  transport->cq =  ibv_create_cq(g_rdma_context->context, MAX_CQE + 1, NULL, transport->comp_channel, 0);
  GPR_ASSERT(transport->cq);
  if (ibv_req_notify_cq(transport->cq, 0) < 0) {
    LOG(ERROR, "ibv_req_notify_cq error, %s", strerror(errno));
    abort();
  }
  transport->poll_pid = create_thread(poll_thread, transport);
  transport->handle_pid = create_thread(handle_thread, transport);
  usleep(100);

  LOG(INFO, "*** now use RC ***");

  struct ibv_qp_init_attr init_attr = {
      .qp_type = IBV_QPT_RC,
      .sq_sig_all = 1,
      .send_cq = transport->cq,
      .recv_cq = transport->cq,
      .srq = NULL,
      .cap = {
          .max_send_wr = MAX_CQE,
          .max_recv_wr = MAX_CQE,
          .max_send_sge = 1,
          .max_recv_sge = 1,
          .max_inline_data = 256,
      },
  };
  transport->rc_qp = ibv_create_qp(g_rdma_context->pd, &init_attr);
  GPR_ASSERT(transport->rc_qp);
  if (modify_qp_to_init(transport->rc_qp) < 0) {
    LOG(ERROR, "rdma_create_connect: failed to modify queue pair to initiate state");
    abort();
  }
  rdma_transport_recv_with_num(transport, MAX_PRE_RECV_QP);

  LOG(DEBUG, "rdma_create_connect end");
  return 0;
}

void rdma_complete_connect(struct rdma_transport *transport) {
  GPR_ASSERT(modify_qp_to_rtr(transport->rc_qp, &transport->remote_qp_attr) == 0);
  GPR_ASSERT(modify_qp_to_rts(transport->rc_qp, &transport->local_qp_attr) == 0);
  LOG(DEBUG, "%s -> %s complete connect", transport->local_ip, transport->remote_ip);
}

void rdma_shutdown_connect(struct rdma_transport *transport) {
  if (transport->poll_pid > 0) {
    pthread_kill(transport->poll_pid, SIGQUIT);
  }
  if (transport->handle_pid > 0) {
    g_queue_free(transport->work_queue);
    pthread_mutex_destroy(&transport->work_queue_lock);
    pthread_cond_destroy(&transport->handle_cv);
    pthread_kill(transport->handle_pid, SIGQUIT);
  }

  GPR_ASSERT(transport->rc_qp);
  if (ibv_destroy_qp(transport->rc_qp) < 0) {
    LOG(ERROR, "ibv_destroy_qp error, %s", strerror(errno));
    abort();
  }
  transport->rc_qp = NULL;

  GPR_ASSERT(transport->cq);
  GPR_ASSERT(transport->comp_channel);
  ibv_destroy_cq(transport->cq);
  ibv_destroy_comp_channel(transport->comp_channel);
  free(transport);

  LOG(DEBUG, "shutdown connect");
}



void rdma_transport_recv_with_num(struct rdma_transport *transport, uint32_t num) {
  struct rdma_chunk *chunk_list = get_rdma_chunk_list_from_pool(g_rdma_context->rbp, num);
  GPR_ASSERT(chunk_list);
  struct ibv_sge *sge = (struct ibv_sge *)calloc(1, sizeof(struct ibv_sge) * num);
  struct ibv_recv_wr *recv_wr = (struct ibv_recv_wr *)calloc(1, sizeof(struct ibv_recv_wr) * num);
  struct rdma_work_chunk *recv_wc;
  for (uint32_t i=0; i<num; i++) {
    recv_wc = (struct rdma_work_chunk *)malloc(sizeof(struct rdma_work_chunk));
    GPR_ASSERT(recv_wc);
    recv_wc->chunk = chunk_list;
    recv_wc->len = RDMA_CHUNK_SIZE;
    recv_wc->transport = transport;

    sge[i].addr = (uint64_t)recv_wc->chunk;
    sge[i].length = recv_wc->len;
    sge[i].lkey = recv_wc->chunk->mr->lkey;

    recv_wr[i].wr_id = (uint64_t)recv_wc;
    recv_wr[i].sg_list = &sge[i];
    recv_wr[i].num_sge = 1;

    chunk_list = chunk_list->next;
  }
  GPR_ASSERT(chunk_list==NULL);
  struct ibv_recv_wr *bad_wr = NULL;
  for (uint32_t i = 0; i < num; ++i) {
    if (ibv_post_recv(transport->rc_qp, &recv_wr[i], &bad_wr) < 0) {
      LOG(ERROR, "ibv_post_recv error, %s", strerror(errno));
      abort();
    }
  }

  free(sge);
  free(recv_wr);
  //LOG(DEBUG, "ib_post_recv %u", num);
}

int rdma_transport_recv(struct rdma_transport *transport) {
  struct rdma_work_chunk *recv_wc = (struct rdma_work_chunk *)malloc(sizeof(struct rdma_work_chunk));
  GPR_ASSERT(recv_wc);
  recv_wc->chunk = get_rdma_chunk_from_pool(g_rdma_context->rbp);
  if (recv_wc->chunk == NULL) {
    LOG(ERROR, "rdma_transport_recv: can't get chunk from pool");
    return -1;
  }
  recv_wc->len = RDMA_CHUNK_SIZE;
  recv_wc->transport = transport;

  struct ibv_sge sge = {
      .addr   = (uint64_t)recv_wc->chunk,
      .length = recv_wc->len,
      .lkey   = recv_wc->chunk->mr->lkey,
  };

  // by the pointer recv_wc->transport, wo can get the pointer of rdma_transport_client/rdma_transport_server
  struct ibv_recv_wr recv_wr = {
      .wr_id   = (uint64_t)recv_wc,
      .sg_list = &sge,
      .num_sge = 1,
  };
  struct ibv_recv_wr *bad_wr = NULL;
  if (ibv_post_recv(transport->rc_qp, &recv_wr, &bad_wr) < 0) {
    LOG(ERROR, "ibv_post_recv error, %s", strerror(errno));
    return -1;
  }
  return 0;
}


int rdma_transport_send(struct rdma_transport *transport, struct rdma_work_chunk *send_wc) {
  struct ibv_sge sge = {
      .addr = (uint64_t)send_wc->chunk,
      .length = send_wc->len + RDMA_HEADER_SIZE,
      .lkey = send_wc->chunk->mr->lkey,
  };
  struct ibv_send_wr send_wr = {
      .wr_id = (uint64_t)send_wc,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_SEND,
      .send_flags = IBV_SEND_SIGNALED,
  };
  struct ibv_send_wr *bad_wr = NULL;
  if (ibv_post_send(transport->rc_qp, &send_wr, &bad_wr) < 0) {
    LOG(ERROR, "ibv_post_send error, %s", strerror(errno));
    abort();
  }
  return 0;
}

// 1. copy rdma to jvm
// 2. call channelRead0 of spark
void work_thread(gpointer data, gpointer user_data) {
  LOG(DEBUG, "work begin");
  struct rdma_transport *server = (struct rdma_transport *)data;
  GPR_ASSERT(server);
  GQueue *work_queue = server->work_queue;

  while (1) {
    pthread_mutex_lock(&server->work_queue_lock);
    varray_t *head = g_queue_peek_head(work_queue);
    if (head == NULL || head->len != head->size) {
      server->is_work_running = 0;
      pthread_mutex_unlock(&server->work_queue_lock);
      break;
    }
    g_queue_pop_head(work_queue);
    pthread_mutex_unlock(&server->work_queue_lock);

    GPR_ASSERT(server->is_work_running == 1);
    if (head->len != head->size/* || head->len == 0*/) {
      LOG(ERROR, "varray len != size (%u, %u)", head->len, head->size);
      abort();
    }

    uint32_t data_len = 0;
    uint32_t len = head->data[0]->header.data_len;
    uint32_t data_id = head->data_id;
    for (uint32_t i=0; i<head->size; i++) {
      GPR_ASSERT(data_id == head->data[i]->header.data_id);
      data_len += head->data[i]->header.chunk_len;
    }
    GPR_ASSERT(len == data_len);

    LOG(DEBUG, "work %u:%u:%s", data_id, len, server->remote_ip);

    jbyteArray jba = jni_alloc_byte_array(data_len);
    int pos = 0;
    for (uint32_t i=0; i<head->size; i++) {
      set_byte_array_region(jba, pos, head->data[i]->header.chunk_len, head->data[i]->body);
      pos += head->data[i]->header.chunk_len;
    }
    jni_channel_callback(server->remote_ip, jba, data_len);

    for (uint32_t i=0; i<head->size; i++) {
      release_rdma_chunk_to_pool(g_rdma_context->rbp, head->data[i]);
    }
    free(head);
    LOG(INFO, "work thread success");
  }
  LOG(DEBUG, "work thread end");
}


/************************************************************************/
/*                          local function                              */
/************************************************************************/


static gint compare_func(gconstpointer a, gconstpointer b) {
  varray_t *x = (varray_t *)a;
  uint32_t y = *(uint32_t *)b;
  if (x->data_id == y)
    return 0;
  else
    return 1;
}

static void *poll_thread(void *arg) {
  LOG(INFO, "poll thread begin");
  struct rdma_transport *server = (struct rdma_transport *)arg;
  GPR_ASSERT(server);
  struct ibv_comp_channel *channel = server->comp_channel;
  struct ibv_cq *cq = server->cq;
  GPR_ASSERT(channel);
  GPR_ASSERT(cq);

  // register quit signal
  signal(SIGQUIT, quit_thread);

  struct ibv_cq *ev_cq;
  void *ev_ctx;
  int event_num = 1;
  dynarray_t *wc_array;

  struct rdma_work_chunk *work_chunk;
  struct ibv_wc *wc;
  uint32_t chunk_num;
  while (1) {
    LOG(DEBUG, "begin blocking ibv_get_cq_event");
    if (ibv_get_cq_event(channel, &ev_cq, &ev_ctx) < 0) {
      LOG(ERROR, "ibv_get_cq_event error, %s", strerror(errno));
      abort();
    }
    ibv_ack_cq_events(cq, 1);
    if (ibv_req_notify_cq(ev_cq, 0) < 0) {
      LOG(ERROR, "ibv_req_notify_cq error, %s", strerror(errno));
      abort();
    }
    do {
      //if (event_num > 0) {
        wc_array = DYNARRY_MALLOC0(MAX_EVENT_PER_POLL * sizeof(struct ibv_wc));
        wc_array->size = MAX_EVENT_PER_POLL;
        wc_array->user_id = (uint64_t) server;
        wc = (struct ibv_wc *) wc_array->data;
      //}
      event_num = ibv_poll_cq(cq, MAX_EVENT_PER_POLL, wc);
      LOG(DEBUG, "poll %d event from %s", event_num, server->remote_ip);

      if (event_num < 0) {
        LOG(ERROR, "ibv_poll_cq poll error");
        abort();
      } else if (event_num > 0) {
        wc_array->len = event_num;
        for (int i = 0; i < event_num; ++i) {
          if (wc->opcode == IBV_WC_RECV) {
            work_chunk = (struct rdma_work_chunk *)wc->wr_id;
            if (work_chunk->chunk->header.chunk_id == 0) {
              chunk_num = work_chunk->chunk->header.chunk_num;
              if (chunk_num == 1) {
                rdma_transport_recv(work_chunk->transport);
              } else {
                rdma_transport_recv_with_num(work_chunk->transport, chunk_num);
              }
            }
          }
          wc++;
        }

        pthread_mutex_lock(&server->handle_queue_lock);
        g_queue_push_tail(server->handle_quque, wc_array);
        pthread_mutex_unlock(&server->handle_queue_lock);

        pthread_mutex_lock(&server->handle_cv_lock);
        if (server->is_handle_running == 0) {
          server->is_handle_running = 1;
          pthread_cond_signal(&server->handle_cv);
          LOG(DEBUG, "awake handle thread");
        }
        pthread_mutex_unlock(&server->handle_cv_lock);
      } else {
        free(wc_array);
      }
    } while (event_num);
  }
  return NULL;
}

static void *handle_thread(void *arg) {
  LOG(INFO, "handle thread begin");
  struct rdma_transport *transport = (struct rdma_transport *)arg;
  GQueue *handle_queue = transport->handle_quque;

  signal(SIGQUIT, quit_thread);

  while (1) {
    pthread_mutex_lock(&transport->handle_queue_lock);
    dynarray_t *head = g_queue_peek_head(handle_queue);
    pthread_mutex_unlock(&transport->handle_queue_lock);

    if (head == NULL) {
      pthread_mutex_lock(&transport->handle_cv_lock);
      transport->is_handle_running = 0;
      pthread_cond_wait(&transport->handle_cv, &transport->handle_cv_lock);
      pthread_mutex_unlock(&transport->handle_cv_lock);
      continue;
    }

    pthread_mutex_lock(&transport->handle_queue_lock);
    g_queue_pop_head(handle_queue);
    pthread_mutex_unlock(&transport->handle_queue_lock);

    GPR_ASSERT(transport->is_handle_running == 1);
    LOG(DEBUG, "handle %d event from %s", head->len, transport->remote_ip);

    struct ibv_wc *wc = (struct ibv_wc *)head->data;
    for (int i=0; i<head->len; i++) {
      if (wc->status != IBV_WC_SUCCESS) {
        LOG(ERROR, "ibv_wc.status error %d", wc->status);
        abort();
      } else {
        if (wc->opcode == IBV_WC_SEND) {
          LOG(DEBUG, "handle send");
          handle_send_event(wc);
        } else if (wc->opcode == IBV_WC_RECV) {
          LOG(DEBUG, "handle recv");
          handle_recv_event(wc);
        } else {
          LOG(ERROR, "ibv_wc.opcode = %d, which is not send or recv", wc->opcode);
          abort();
        }
      }
      wc++;
    }
    free(head);
  }
}


static void handle_send_event(struct ibv_wc *wc) {
  struct rdma_work_chunk *work_chunk = (struct rdma_work_chunk *)wc->wr_id;
  struct rdma_chunk *chunk = work_chunk->chunk;
  release_rdma_chunk_to_pool(g_rdma_context->rbp, chunk);
  free(work_chunk);
}

// producer thread and the chunk of data is order
static void handle_recv_event(struct ibv_wc *wc) {
  struct rdma_work_chunk *work_chunk = (struct rdma_work_chunk *)wc->wr_id;
  struct rdma_transport *server = work_chunk->transport;
  struct rdma_chunk *chunk = work_chunk->chunk;
  uint32_t data_id = chunk->header.data_id;

  if (chunk->header.chunk_len + RDMA_HEADER_SIZE != wc->byte_len) {
    LOG(ERROR, "remote_ip:%s local_ip:%s, the data (id:%u, num:%u) send chunk %u byte, but recv %u byte",
        server->remote_ip, server->local_ip, chunk->header.data_id, chunk->header.chunk_num,
        chunk->header.chunk_len + RDMA_HEADER_SIZE, wc->byte_len);
    abort();
  }

  // 目前设计只会有一个线程对chache操作
  // cache根据data_id缓冲，向线程池提交的参数是value可变长数组
  pthread_mutex_lock(&server->work_queue_lock);
  GList *found = g_queue_find_custom(server->work_queue, &data_id, compare_func);
  pthread_mutex_unlock(&server->work_queue_lock);
  if (found == NULL) {
    // free by worker, donot free by this function and hash table
    varray_t *now = VARRY_MALLOC0(work_chunk->chunk->header.chunk_num);
    GPR_ASSERT(now);
    now->transport = server;
    now->data_id = chunk->header.data_id;
    now->size = chunk->header.chunk_num;
    now->data[now->len++] = chunk;
    LOG(DEBUG, "recv id %u:%u:%s", data_id, chunk->header.data_len, server->remote_ip);
    pthread_mutex_lock(&server->work_queue_lock);
    g_queue_push_tail(server->work_queue, now);
    pthread_mutex_unlock(&server->work_queue_lock);
  } else {
    varray_t *now = found->data;
    if (now->len >= now->size) {
      LOG(ERROR, "remote_ip:%s local_ip:%s, the data (id:%u, num:%u) when push chunk, len >= size (%u, %u)",
          server->remote_ip, server->local_ip, chunk->header.data_id, chunk->header.chunk_num, now->len, now->size);
      abort();
    }
    if (now->data_id != chunk->header.data_id) {
      LOG(ERROR, "%s --> %s, data_id: %u != %u",
          server->remote_ip, server->local_ip, now->data_id, chunk->header.data_id);
      abort();
    }
    now->data[now->len++] = chunk;
  }
  pthread_mutex_lock(&server->work_queue_lock);
  varray_t *head = g_queue_peek_head(server->work_queue);
  if (server->is_work_running == 0 && head && head->len == head->size) {
    server->is_work_running = 1;
    LOG(INFO, "awake work thread### -> %u:%u:%s", head->data_id,head->data[0]->header.data_len, server->remote_ip);
    g_thread_pool_push(g_rdma_context->work_thread_pool, server, NULL);
  }
  pthread_mutex_unlock(&server->work_queue_lock);

  free(work_chunk);
}
