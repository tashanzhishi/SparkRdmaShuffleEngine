

gcc -g $1 `pkg-config --cflags --libs glib-2.0` `pkg-config --cflags --libs gthread-2.0` -lpthread
