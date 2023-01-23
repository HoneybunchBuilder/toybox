file(GLOB_RECURSE bistro_source "source/*.c")

tb_add_app(tb_bistro ${bistro_source})
target_link_libraries(tb_bistro PRIVATE tb_samplecore)
