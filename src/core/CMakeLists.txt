add_library(vtcore STATIC
    src/Schema.cpp
    src/VtObject.cpp
    src/ObjectPool.cpp
    src/PoolEditing.cpp
    src/IopCodec.cpp
    src/Hierarchy.cpp
    src/Validator.cpp
    src/Palette.cpp
    src/PictureCodec.cpp
    src/Exporters.cpp
    src/ProjectFile.cpp
)

target_include_directories(vtcore PUBLIC include)
target_link_libraries(vtcore PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(vtcore PUBLIC cxx_std_20)
