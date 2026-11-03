idf_component_register(SRCS "hooks.c" REQUIRES bootloader bootloader_support)
target_include_directories(${COMPONENT_LIB} PRIVATE "$ENV{IDF_PATH}/components/bootloader_support/private_include")
