#include <glib.h>
#include <stdio.h>
#define N 100

int main()
{
    //GChecksum *cs = g_checksum_new(G_CHECKSUM_MD5);

    char data[N];
    for (int i=0; i<N; i++) {
        data[i] = '0'+(i%10);
    }
    //data[N-1] = '\0';

    //printf("%s\n", g_checksum_get_string(cs));
    printf("%s\n", g_compute_checksum_for_string(G_CHECKSUM_MD5, data, sizeof(data)));
    printf("%s\n", g_compute_checksum_for_data(G_CHECKSUM_MD5, data, sizeof(data)));

    //g_checksum_free(cs);
}