cmake_minimum_required(VERSION 3.7)
project(as02)

set(CMAKE_C_STANDARD 11)

# enable pthread like this:
# https://stackoverflow.com/a/29871891/
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CMAKE_CXX_FLAGS "#{CMAKE_CXX_FLAGS} -Wall -Werror")
set(SOURCE_FILES main.c LIST.h LIST.c)
add_executable(as02 ${SOURCE_FILES})

target_link_libraries(as02 Threads::Threads)