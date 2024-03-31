option(TB_ADDON_WATER "Use the Water addon" ON)

# Some common addon functionality goes here

if(TB_ADDON_WATER)
  add_subdirectory(water)
endif()