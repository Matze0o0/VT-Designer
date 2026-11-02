add_library(vtrender STATIC
    include/vtrender/VtText.h
    include/vtrender/VtRenderer.h
    src/VtText.cpp
    src/VtRenderer.cpp
)

target_include_directories(vtrender PUBLIC include)
target_link_libraries(vtrender PUBLIC vtcore Qt6::Gui)
