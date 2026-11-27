add_executable(oly src/main.cpp)
target_compile_features(oly PRIVATE cxx_std_20)
target_compile_definitions(oly PRIVATE OLY_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(oly PRIVATE Oly::core Oly::warnings)
