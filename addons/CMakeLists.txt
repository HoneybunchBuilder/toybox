option(TB_ADDON_WATER "Use the Water addon" ON)

# Some common addon functionality goes here

if(TB_ADDON_WATER)
  include(${CMAKE_CURRENT_LIST_DIR}/water/CMakeLists.txt)
endif()