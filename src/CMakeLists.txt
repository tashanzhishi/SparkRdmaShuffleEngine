cmake_minimum_required(VERSION 2.6)

set(SOURCE_FILES thread.c
        rdma_buffer_pool.c rdma_log.c rdma_utils.c
        rdma_transport_client.c rdma_transport_server.c)

# 添加动态库 关键词SHARED
add_library(spark_rdma_shuffle_engine SHARED ${SOURCE_FILES})
# 添加静态库 关键词STATIC
add_library(spark_rdma_shuffle_engine_static STATIC ${SOURCE_FILES})
# 重命名
set_target_properties(spark_rdma_shuffle_engine_static PROPERTIES OUTPUT_NAME "spark_rdma_shuffle_engine")

install(TARGETS spark_rdma_shuffle_engine spark_rdma_shuffle_engine_static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
#install(FILES pow.h DESTINATION /tmp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB_PKG glib-2.0)
include_directories(${GLIB_PKG_INCLUDE_DIRS})

# 加入REQUIRED 参数,如果找不到JNI,就报错退出
set(JAVA_HOME "/opt/java/jdk1.8.0_111")
find_package(JNI REQUIRED)
# 加入jni支持
include_directories(${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})


#target_link_libraries(${PROJECT_NAME} ibverbs ${GLIB_LIBRARIES})
