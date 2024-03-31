file(GLOB_RECURSE ocean_source "source/*.c")

# Must pass source as a string or else it won't properly be interpreted as a list
tb_add_app(tb_ocean "${ocean_source}")
tb_use_addon(tb_ocean tb_water)
target_link_libraries(tb_ocean PRIVATE tb_samplecore)
