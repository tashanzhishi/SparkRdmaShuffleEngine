cmake_minimum_required(VERSION 3.5)

set(SOURCE_FILES thread.c
        rdma_buffer_pool.c RdmaMsgHeader.c rdma_log.c rdma_utils.c
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

target_link_libraries(${PROJECT_NAME} ibverbs)
