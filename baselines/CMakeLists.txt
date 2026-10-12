# Included from the artifact root; requires C++20 and ZLIB::ZLIB.
add_executable(udshp
  udshp/run.cpp
  udshp/core/DynamicGraph-singleInstance.cpp
  udshp/core/DynOpManager-singleInstance.cpp
  udshp/include/EdgeManager.cpp)
target_include_directories(udshp PRIVATE udshp/core udshp/include common)
target_compile_definitions(udshp PRIVATE HB_PROFILE=0)
target_link_libraries(udshp PRIVATE ZLIB::ZLIB)
target_compile_features(udshp PRIVATE cxx_std_20)
add_executable(cq
  cq/run.cpp)
target_include_directories(cq PRIVATE common)
target_link_libraries(cq PRIVATE ZLIB::ZLIB)
target_compile_features(cq PRIVATE cxx_std_20)
add_executable(improved_dynopt
  ordinary/improved_dynopt/run.cpp
  ordinary/improved_dynopt/lib/algorithms/dyn_edge_orientation_improved_opt.cpp
  ordinary/improved_dynopt/lib/algorithms/dyn_edge_orientation_improved_opt_fixed.cpp
  ordinary/improved_dynopt/lib/algorithms/dyn_edge_orientation_strong_opt.cpp)
target_include_directories(improved_dynopt PRIVATE ordinary/improved_dynopt/lib ordinary/improved_dynopt/lib/algorithms)
target_compile_features(improved_dynopt PRIVATE cxx_std_20)
add_executable(insdel
  ordinary/insdel/run.cpp)
target_include_directories(insdel PRIVATE ordinary/insdel)
target_compile_features(insdel PRIVATE cxx_std_20)
