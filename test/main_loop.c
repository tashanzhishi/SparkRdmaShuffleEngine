#include <glib.h>
#include <stdio.h>


gboolean source_prepare_cb(GSource *source, gint *timeout) {
  printf("prepare\n");
  *timeout = 1000;
  return FALSE;
}

gboolean source_check_cb(GSource *source) {
  printf("check\n");
  return TRUE;
}

gboolean source_dispatch_cb(GSource *source,
                            GSourceFunc callback, gpointer data) {
  printf("dispatch\n");
  return TRUE;
}

void source_finalize_cb(GSource *source) {
  printf("finalize\n");
}

int main(int argc, char *argv[]) {
  GMainLoop *mainloop;
  GMainContext *maincontext;
  GSource *source;
  GSourceFuncs sourcefuncs;

  sourcefuncs.prepare = source_prepare_cb;
  sourcefuncs.check = source_check_cb;
  sourcefuncs.dispatch = source_dispatch_cb;
  sourcefuncs.finalize = source_finalize_cb;

  mainloop = g_main_loop_new(NULL, FALSE);
  maincontext = g_main_loop_get_context(mainloop);
  source = g_source_new(&sourcefuncs, sizeof(GSource));
  g_source_attach(source, maincontext);

  g_main_loop_run(mainloop);

  return 0;
}
