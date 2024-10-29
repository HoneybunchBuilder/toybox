# Some ports are not okay with using clang
if(NOT PORT MATCHES "^(dbus)$")
  set(CMAKE_C_COMPILER "clang")
  set(CMAKE_CXX_COMPILER "clang++")
endif()