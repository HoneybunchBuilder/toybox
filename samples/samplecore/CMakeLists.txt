file(GLOB_RECURSE samplecore_source "source/*.c")

add_library(tb_samplecore STATIC ${samplecore_source})

target_include_directories(tb_samplecore PUBLIC "include")
target_link_libraries(tb_samplecore PUBLIC toybox)
