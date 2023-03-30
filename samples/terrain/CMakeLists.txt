file(GLOB_RECURSE terrain_source "source/*.c")

# Must pass source as a string or else it won't properly be interpreted as a list
tb_add_app(tb_terrain "${terrain_source}")
target_link_libraries(tb_terrain PRIVATE tb_samplecore)
