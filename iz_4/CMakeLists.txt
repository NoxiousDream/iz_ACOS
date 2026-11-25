cmake_minimum_required(VERSION 4.3)
project(iz_ACOS C)

set(CMAKE_C_STANDARD 11)

add_executable(iz_4
        hospital_proto.h
        hospital_server.c
        hospital_doctor.c
        hospital_patient.c
        hospital_monitor.c)
