# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/KOutNet_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/KOutNet_autogen.dir/ParseCache.txt"
  "KOutNet_autogen"
  )
endif()
