#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

struct myvalue {
	int a;
	char s[20];
};

void print_key_value(gpointer key, gpointer value ,gpointer user_data) {
  printf("%ld --> %ld\n",*(int64_t*)key, *(int64_t*)value);
}

static void free_data(gpointer kv) {
	g_free(kv);
	kv = NULL;
}

void test1(GHashTable *tab) {
	//char ip11[]="172.16.0.11"; uint64_t x = 11;
	//char ip12[]="172.16.0.12"; uint64_t y = 12;
	//char ip13[]="172.16.0.13"; uint64_t z = 13;
	char *ip11=strdup("172.16.0.11");
	struct myvalue *x = (struct myvalue*)malloc(sizeof(struct myvalue)); x->a = 11;
	char *ip12=strdup("172.16.0.12");
	struct myvalue *y = (struct myvalue*)malloc(sizeof(struct myvalue)); y->a = 12;
	char *ip13=strdup("172.16.0.13");
	struct myvalue *z = (struct myvalue*)malloc(sizeof(struct myvalue)); z->a = 13;

	g_hash_table_insert(tab, ip11, x);
	g_hash_table_insert(tab, ip12, y);
	g_hash_table_insert(tab, ip13, z);
}

void test2(GHashTable *tab) {
	char ip11[]="172.16.0.12";
	struct myvalue *ret = g_hash_table_lookup(tab, ip11);
	printf("%s %p\n", ip11, ret );
	if (ret) {
		printf("%s %d\n", ip11, ret->a);
	}
}

void test3(GHashTable *ha) {
  int64_t *a = (int64_t *)malloc(sizeof(int64_t)); *a = 3;
  int64_t *b = (int64_t *)malloc(sizeof(int64_t)); *b = 4;
  g_hash_table_insert(ha, a, b);

  int64_t *c = (int64_t *)malloc(sizeof(int64_t)); *c = 4;
  //int64_t *d = (int64_t *)malloc(sizeof(int64_t)); *d = 4;
  struct myvalue *d = (struct myvalue *)malloc(sizeof(struct myvalue));
  d->a = 100; strcpy(d->s, "hello,world");
  g_hash_table_insert(ha, c, d);
}
void test4(GHashTable *ha) {
  int64_t key = 4;
  struct myvalue *value = g_hash_table_lookup(ha, &key);
  printf("%ld -> (%d, %s)\n", key, value->a, value->s);

  key =10;
  printf("%p\n", g_hash_table_lookup(ha, &key));

  g_hash_table_foreach(ha, print_key_value, NULL);

  g_hash_table_remove(ha, &key);
  g_hash_table_foreach(ha, print_key_value, NULL);
}

int main() {
	//GHashTable *tab = g_hash_table_new_full(g_str_hash, g_int64_equal, free_data, free_data);
	//test1(tab);
	//test2(tab);
	//g_hash_table_destroy(tab);

  //GHashTable *ha = g_hash_table_new(g_int64_hash, g_int64_equal);
  GHashTable *ha = g_hash_table_new_full(g_int64_hash, g_int64_equal, free_data, free_data);
  test3(ha);
  test4(ha);
  g_hash_table_destroy(ha);
	return 0;
}
