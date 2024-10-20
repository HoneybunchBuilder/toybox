
option(TB_COOK_ASSETS "Process assets for runtime loading" ON)

set(engine_shader_include_dir "${CMAKE_SOURCE_DIR}/include")

# Helper function to cook shaders
function(tb_cook_shaders out_shader_sources out_shader_headers)

  # Compile Shaders
  set(shader_include_dir "${CMAKE_CURRENT_LIST_DIR}/include")
  file(GLOB shader_includes CONFIGURE_DEPENDS "${shader_include_dir}/*.hlsli" "${shader_include_dir}/*.h")

  file(GLOB slang_shaders CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.slang")
  foreach(shader ${slang_shaders})
    get_filename_component(filename ${shader} NAME_WLE)
    set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

    # Outputting short names for shader spv modules because due to xxd nonsense
    # the name of the variable in the C header will be based on this file name
    set(vert_out_path "${filename}_vert")
    set(frag_out_path "${filename}_frag")
    set(out_paths "${vert_out_path};${frag_out_path}")

    add_custom_command(
        OUTPUT ${out_paths}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
        COMMAND ${SLANG} -DTB_SHADER=1 -profile sm_6_5 -stage vertex -entry vert -target spirv $<$<CONFIG:Debug>:-O0> $<$<NOT:$<CONFIG:Release>>:-g> -I ${shader_include_dir} -I ${engine_shader_include_dir} -o ${vert_out_path} ${shader}
        COMMAND ${SLANG} -DTB_SHADER=1 -profile sm_6_5 -stage fragment -entry frag -target spirv $<$<CONFIG:Debug>:-O0> $<$<NOT:$<CONFIG:Release>>:-g> -I ${shader_include_dir} -I ${engine_shader_include_dir} -o ${frag_out_path} ${shader}
        MAIN_DEPENDENCY ${shader}
        DEPENDS ${shader_includes}
    )

    set(vert_header "${shader_out_path}/${filename}_vert.h")
    set(frag_header "${shader_out_path}/${filename}_frag.h")

    # Use xxd to convert spv binary files to C headers that can be included
    # Renaming shenanigans to work around old xxd versions not supporting the 
    # '-n' flag to rename the C variable
    add_custom_command(
        OUTPUT ${vert_header}
        COMMAND xxd -i ${vert_out_path} ${vert_header}
        MAIN_DEPENDENCY ${vert_out_path}
    )
    add_custom_command(
        OUTPUT ${frag_header}
        COMMAND xxd -i ${frag_out_path} ${frag_header}
        MAIN_DEPENDENCY ${frag_out_path}
    )

    list(APPEND shader_headers ${vert_header} ${vert_out_path})
    list(APPEND shader_headers ${frag_header} ${frag_out_path})
  endforeach()

  file(GLOB slang_compute_shaders CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.slangc")
  foreach(shader ${slang_compute_shaders})
    get_filename_component(filename ${shader} NAME_WLE)
    set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

    # Outputting short names for shader spv modules because due to xxd nonsense
    # the name of the variable in the C header will be based on this file name
    set(comp_out_path "${filename}_comp")
    set(out_paths "${comp_out_path};")

    add_custom_command(
        OUTPUT ${out_paths}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
        COMMAND ${SLANG} -DTB_SHADER=1 -lang slang -profile sm_6_5 -stage compute -entry comp -target spirv $<$<CONFIG:Debug>:-O0> $<$<NOT:$<CONFIG:Release>>:-g> -I ${shader_include_dir} -I ${engine_shader_include_dir} -o ${comp_out_path} ${shader}
        MAIN_DEPENDENCY ${shader}
        DEPENDS ${shader_includes}
    )

    set(comp_header "${shader_out_path}/${filename}_comp.h")

    # Use xxd to convert spv binary files to C headers that can be included
    # Renaming shenanigans to work around old xxd versions not supporting the 
    # '-n' flag to rename the C variable
    add_custom_command(
        OUTPUT ${comp_header}
        COMMAND xxd -i ${comp_out_path} ${comp_header}
        MAIN_DEPENDENCY ${comp_out_path}
    )

    list(APPEND shader_headers ${comp_header} ${comp_out_path})
  endforeach()

  file(GLOB slang_mesh_shaders CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/source/*.slangm")
  foreach(shader ${slang_mesh_shaders})
    get_filename_component(filename ${shader} NAME_WLE)
    set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

   # Outputting short names for shader spv modules because due to xxd nonsense
    # the name of the variable in the C header will be based on this file name
    set(mesh_out_path "${filename}_mesh")
    set(frag_out_path "${filename}_frag")
    set(out_paths "${mesh_out_path};${frag_out_path}")

    add_custom_command(
        OUTPUT ${out_paths}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
        COMMAND ${SLANG} -DTB_SHADER=1 -lang slang -profile sm_6_5 -stage mesh -entry mesh -target spirv $<$<CONFIG:Debug>:-O0> $<$<NOT:$<CONFIG:Release>>:-g> -I ${shader_include_dir} -I ${engine_shader_include_dir} -o ${mesh_out_path} ${shader}
        COMMAND ${SLANG} -DTB_SHADER=1 -lang slang -profile sm_6_5 -stage fragment -entry frag -target spirv $<$<CONFIG:Debug>:-O0> $<$<NOT:$<CONFIG:Release>>:-g> -I ${shader_include_dir} -I ${engine_shader_include_dir} -o ${frag_out_path} ${shader}
        MAIN_DEPENDENCY ${shader}
        DEPENDS ${shader_includes}
    )

    set(mesh_header "${shader_out_path}/${filename}_mesh.h")
    set(frag_header "${shader_out_path}/${filename}_frag.h")

    # Use xxd to convert spv binary files to C headers that can be included
    # Renaming shenanigans to work around old xxd versions not supporting the 
    # '-n' flag to rename the C variable
    add_custom_command(
        OUTPUT ${mesh_header}
        COMMAND xxd -i ${mesh_out_path} ${mesh_header}
        MAIN_DEPENDENCY ${mesh_out_path}
    )
    add_custom_command(
        OUTPUT ${frag_header}
        COMMAND xxd -i ${frag_out_path} ${frag_header}
        MAIN_DEPENDENCY ${frag_out_path}
    )

    list(APPEND shader_headers ${mesh_header} ${mesh_out_path})
    list(APPEND shader_headers ${frag_header} ${frag_out_path})
  endforeach()

  set("${out_shader_sources}" "${shader_sources}" PARENT_SCOPE)
  set("${out_shader_headers}" "${shader_headers}" PARENT_SCOPE)
endfunction()

# Helper function to cook assets
function(tb_cook_assets target_name out_assets_path)
  set(assets_dest "assets")
  if(ANDROID)
    set(assets_dest "$<CONFIG>/assets")
    install(TARGETS ${out_target_name}
            RUNTIME DESTINATION "$<CONFIG>/lib/${arch}"
            LIBRARY DESTINATION "$<CONFIG>/lib/${arch}")
  elseif(APPLE AND NOT IOS)
    # Have to do this or else it won't be placed in the right part of the app package
    install(TARGETS ${out_target_name}
      RUNTIME DESTINATION "./../MacOS/"
      BUNDLE DESTINATION "./../MacOS/")
  else()
    install(TARGETS ${out_target_name}
            RUNTIME DESTINATION "."
            BUNDLE DESTINATION "."
            LIBRARY DESTINATION ".")
  endif()

  # Cook scenes
  file(GLOB_RECURSE source_scenes CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/scenes/*.glb")
  foreach(scene ${source_scenes})
    file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${scene})
    get_filename_component(relpath ${relpath} DIRECTORY)
    get_filename_component(filename ${scene} NAME_WE)
    set(packed_scene ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/scenes/${filename}.glb)

    add_custom_command(
      OUTPUT ${packed_scene}
      COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
      COMMAND ${GLTFPACK} -cc -kn -km -ke -tc -i ${scene} -o ${packed_scene}
      MAIN_DEPENDENCY ${scene}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

    list(APPEND packed_scenes ${packed_scene})
  endforeach()
  list(APPEND assets ${packed_scenes})
  add_custom_target(${target_name}_scenes DEPENDS ${packed_scenes})

  # Copy textures
  file(GLOB_RECURSE source_textures CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/textures/*.ktx2")
  foreach(texture ${source_textures})
    file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${texture})
    get_filename_component(relpath ${relpath} DIRECTORY)
    get_filename_component(filename ${texture} NAME_WE)
    set(packed ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/textures/${filename}.ktx2)

    message("Copying ${texture} to ${packed}")
    add_custom_command(
      OUTPUT ${packed}
      COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
      COMMAND ${CMAKE_COMMAND} -E copy ${texture} ${packed}
      MAIN_DEPENDENCY ${texture}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

    list(APPEND packed_textures ${packed})
  endforeach()
  list(APPEND assets ${packed_textures})
  add_custom_target(${target_name}_textures DEPENDS ${packed_textures})

  # Copy audio
  file(GLOB_RECURSE source_audio CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/audio/*.ogg" "${CMAKE_CURRENT_LIST_DIR}/assets/audio/*.wav")
  foreach(audio ${source_audio})
    file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${audio})
    get_filename_component(relpath ${relpath} DIRECTORY)
    get_filename_component(filename ${audio} NAME)
    set(packed ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/audio/${filename})

    message("Copying ${audio} to ${packed}")
    add_custom_command(
      OUTPUT ${packed}
      COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
      COMMAND ${CMAKE_COMMAND} -E copy ${audio} ${packed}
      MAIN_DEPENDENCY ${audio}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

    list(APPEND packed_audio ${packed})
  endforeach()
  list(APPEND assets ${packed_audio})
  add_custom_target(${target_name}_audio DEPENDS ${packed_audio})

  # Generate asset database
  set(source_assets ${packed_scenes} ${packed_textures} ${packed_audio})
  list(LENGTH source_assets ASSET_NUM)
  set(generated_manifest "${CMAKE_CURRENT_BINARY_DIR}/config/${target_name}_assetmanifest.h")
  # Add all assets to asset list
  # Also keep track of which assets are scenes
  math(EXPR asset_idx "0")
  foreach(asset ${source_assets})
    get_filename_component(asset_ext ${asset} EXT BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
    get_filename_component(asset_path ${asset} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
    file(RELATIVE_PATH relpath ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets ${asset_path})
    if(asset_ext MATCHES ".glb")
      list(APPEND scene_index_list ${asset_idx})
    endif()
    string(APPEND ASSET_LIST "\"${relpath}\",")
    math(EXPR asset_idx "${asset_idx} + 1")
  endforeach()
  # Add all scene indexes to the string that will be sent to the generated file
  list(LENGTH scene_index_list SCENE_NUM)
  foreach(idx ${scene_index_list})
    string(APPEND SCENE_LIST "${idx},")
  endforeach()

  configure_file(${CMAKE_SOURCE_DIR}/source/tb_assetmanifest.h.in ${generated_manifest})
  add_custom_target(${target_name}_asset_manifest DEPENDS ${generated_manifest})

  add_dependencies(${target_name} ${target_name}_scenes ${target_name}_textures ${target_name}_audio ${target_name}_asset_manifest)

  # Install cooked assets
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets
                    DESTINATION ${assets_dest})

  # Add a define to the target so we know that assets were cooked
  # if they weren't we shouldn't even try to load the manifest
  target_compile_definitions(${target_name} PRIVATE -DTB_COOKED=1)

  set(${out_assets_path} ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets PARENT_SCOPE)
endfunction()
