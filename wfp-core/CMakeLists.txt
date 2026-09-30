cmake_minimum_required(VERSION 3.16)
project(wfp-core C)

add_library(wfp-core STATIC src/wfp.c)
target_include_directories(wfp-core PUBLIC include)
set_target_properties(wfp-core PROPERTIES C_STANDARD 99)
