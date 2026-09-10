cmake_minimum_required(VERSION 3.24)
project(SislConsumer LANGUAGES CXX)

find_package(Sisl 0.1 REQUIRED)

add_executable(sisl_consumer main.cpp)
target_link_libraries(sisl_consumer PRIVATE Sisl::Sisl)
