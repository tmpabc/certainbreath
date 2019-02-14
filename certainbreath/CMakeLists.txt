cmake_minimum_required(VERSION 3.7)
project(certainbreath)

set(CMAKE_CXX_STANDARD 14)


add_executable(certainbreath.out main.cpp)

find_library(wiringPi_LIB wiringPi)
find_library(crypt_LIB crypt)
find_library(pthread_LIB pthread)
find_library(mman_LIB rt)
find_library(curl_LIB curl)

target_link_libraries(certainbreath.out ${wiringPi_LIB} ${crypt_LIB} ${pthread_LIB} ${mman_LIB} ${curl_LIB})

