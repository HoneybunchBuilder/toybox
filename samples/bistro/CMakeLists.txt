file(GLOB_RECURSE bistro_source "source/*.c")

# Must pass source as a string or else it won't properly be interpreted as a list
tb_add_app(tb_bistro "${bistro_source}")
target_link_libraries(tb_bistro PRIVATE tb_samplecore)
