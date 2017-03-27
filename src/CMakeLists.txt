cmake_minimum_required(VERSION 3.5)

set(SOURCE_FILES HashCode.c HashTable.c LinkQueue.c Thread.c
                 RdmaBufferPool.c RdmaMsgHeader.c RdmaCommon.c RdmaUtils.c
                 RdmaTransportClient.c RdmaTransportServer.c)

# 添加动态库 关键词SHARED
add_library(spark_rdma_shuffle_engine SHARED ${SOURCE_FILES})
# 添加静态库 关键词STATIC
add_library(spark_rdma_shuffle_engine_static STATIC ${SOURCE_FILES})
# 重命名
set_target_properties(pow_static PROPERTIES OUTPUT_NAME "spark_rdma_shuffle_engine")

install(TARGETS pow pow_static
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
#install(FILES pow.h DESTINATION /tmp)

target_link_libraries(${PROJECT_NAME} ibverbs)
