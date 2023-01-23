file(GLOB_RECURSE boat_source "source/*.c")

tb_add_app(tb_boat ${boat_source})
target_link_libraries(tb_boat PRIVATE tb_samplecore)
