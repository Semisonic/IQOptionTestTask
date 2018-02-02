# cmake_minimum_required(VERSION <specify CMake version here>)
project(IQOptionTestTask)

set(CMAKE_CXX_STANDARD 17)
include_directories(lib/asio-1.10.8/include)
add_definitions(-DASIO_STANDALONE)

add_executable(IQOptionTestTask service/main.cpp ipc/protocol.h service/core_data.h utils/spinlock.h service/message_dispatcher.cpp service/message_dispatcher.h service/rating_announcer.h service/rating_announcer.cpp service/rating_calculator.cpp service/rating_calculator.h service/job_queue.cpp service/job_queue.h service/worker_pool.cpp service/worker_pool.h ipc/transport.h utils/types.h utils/date_time.h utils/binary_storage.h service/message_builder.h service/overseer.cpp service/overseer.h)
target_link_libraries(IQOptionTestTask ws2_32)