cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hueleaf_gallery)
message(WARNING "HueLeaf gallery: do NOT use `idf.py flash` - it writes the app to 0x10000 "
                "(factory = stock firmware). Use tools/package.sh + dist/install/flash_install.sh.")
