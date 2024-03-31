# Setup water addon
file(GLOB_RECURSE water_source CONFIGURE_DEPENDS 
  "${CMAKE_CURRENT_LIST_DIR}/source/*.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/source/*.h")
tb_new_addon(tb_water "${water_source}")
