cmake_minimum_required(VERSION 4.3)
project(server_epoll LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
if(NOT DEFINED MAX_FD_COUNT)
	execute_process(COMMAND sh -c "ulimit -n"
			OUTPUT_VARIABLE MAX_FD_COUNT
			OUTPUT_STRIP_TRAILING_WHITESPACE)
	message(WARNING "It's recommended to configure a maximum file descriptor count. The default limit in your environment is ${MAX_FD_COUNT}")
endif()
add_definitions(-DMAX_FD_COUNT=${MAX_FD_COUNT})
file(GLOB SOURCES *.cpp)
add_executable(server_epoll.out ${SOURCES})
