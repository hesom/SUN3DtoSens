cmake_minimum_required(VERSION 3.5)
project(SUN3DtoSens)

add_executable(SUN3DtoSens
    src/main.cpp
    src/stb_image.h
    src/stbi_image_write.h
)
set_property(TARGET SUN3DtoSens PROPERTY CXX_STANDARD 17)
set_property(TARGET SUN3DtoSens PROPERTY CXX_STANDARD_REQUIRED ON)