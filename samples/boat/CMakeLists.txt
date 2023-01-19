file(GLOB_RECURSE boat_source "source/*.c")

tb_add_app(tb_boat ${boat_source})
target_link_libraries(tb_boat PRIVATE tb_samplecore)

file(GLOB_RECURSE scenes CONFIGURE_DEPENDS "${CMAKE_BINARY_DIR}/*/assets/scenes/*.glb")
file(GLOB_RECURSE textures CONFIGURE_DEPENDS "${CMAKE_BINARY_DIR}/*/assets/textures/*.ktx2")
set(engine_assets "")
foreach(engine_scene ${scenes})
    get_filename_component(filename ${engine_scene} NAME_WE)
    set(asset_copy ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/scenes/${filename}.glb)

    add_custom_command(
        OUTPUT ${asset_copy}
        COMMAND ${CMAKE_COMMAND} -E copy ${engine_scene} ${asset_copy}
        MAIN_DEPENDENCY ${engine_scene}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

    list(APPEND engine_assets ${asset_copy})
endforeach()
foreach(engine_texture ${textures})
    get_filename_component(filename ${engine_texture} NAME_WE)
    set(asset_copy ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/textures/${filename}.ktx2)

    add_custom_command(
        OUTPUT ${asset_copy}
        COMMAND ${CMAKE_COMMAND} -E copy ${engine_texture} ${asset_copy}
        MAIN_DEPENDENCY ${engine_texture}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

    list(APPEND engine_assets ${asset_copy})
endforeach()
add_custom_target(engine_asset_copy_boat DEPENDS ${engine_assets})
add_dependencies(tb_boat engine_asset_copy_boat)
