add_library(messager_lib
    src/core/types.h
    src/core/nextalkbrand.h
    src/core/nextalkbrand.cpp
    src/services/chatservice.h
    src/services/nextalkmapping.h
    src/services/nextalkmapping.cpp
    src/services/mockservice.h
    src/services/mockservice.cpp
    src/controllers/appcontroller.h
    src/controllers/appcontroller.cpp
    src/ui/theme.h
    src/ui/theme.cpp
    src/ui/pencilskin.h
    src/ui/pencilskin.cpp
    src/ui/components.h
    src/ui/components.cpp
    src/ui/pagewidgets.h
    src/ui/pagewidgets.cpp
    src/ui/pages.cpp
    src/ui/liveui.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp)
target_include_directories(messager_lib PUBLIC src PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(messager_lib PUBLIC Qt6::Widgets)
if(NEXTALK_WITH_CALLS)
    target_sources(messager_lib PRIVATE src/features/calls/callmanager.cpp)
    target_link_libraries(messager_lib PUBLIC Qt6::Multimedia)
else()
    target_sources(messager_lib PRIVATE src/features/calls/callmanager_unavailable.cpp)
endif()
