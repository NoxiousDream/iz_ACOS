cmake_minimum_required(VERSION 4.3)
project(iz_ACOS C)

set(CMAKE_C_STANDARD 11)

add_executable(iz_3 hospital_named.c
        hospital_unnamed.c
        hospital_ind_shm.c
        hospital_ind_pipe.c)
