#!/bin/sh

export G_DEBUG=gc-friendly
export G_SLICE=always-malloc,debug-blocks

#valgrind --tool=massif --depth=5  --alloc-fn=g_malloc \
#  --alloc-fn=g_realloc --alloc-fn=g_try_malloc \
#  --alloc-fn=g_malloc0 --alloc-fn=g_mem_chunk_alloc \
#   --log-file=/var/log/ck2-valgrind-massif.log \
#  console-kit-daemon

valgrind --tool=memcheck --leak-check=full --show-reachable=yes \
    --leak-resolution=high --num-callers=500 \
    --track-origins=yes --read-var-info=yes \
    --show-leak-kinds=all --log-file=/var/log/ck2-valgrind-memcheck.log \
  console-kit-daemon
