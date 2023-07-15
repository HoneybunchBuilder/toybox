file(GLOB_RECURSE viewer_source CONFIGURE_DEPENDS "source/*.c")

# Must pass source as a string or else it won't properly be interpreted as a list
tb_add_app(tb_viewer "${viewer_source}")
