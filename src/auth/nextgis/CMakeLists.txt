set(AUTH_NEXTGIS_SRCS
  core/qgsauthnextgismethod.cpp
)

set(AUTH_NEXTGIS_HDRS
  core/qgsauthnextgismethod.h
)

set(AUTH_NEXTGIS_UIS_H "")

if (WITH_GUI)
  set(AUTH_NEXTGIS_SRCS ${AUTH_NEXTGIS_SRCS}
    gui/qgsauthnextgisedit.cpp
  )
  set(AUTH_NEXTGIS_HDRS ${AUTH_NEXTGIS_HDRS}
    gui/qgsauthnextgisedit.h
  )
  set(AUTH_NEXTGIS_UIS gui/qgsauthnextgisedit.ui)
  qt_wrap_ui(AUTH_NEXTGIS_UIS_H ${AUTH_NEXTGIS_UIS})
endif()


# static library
add_library(authmethod_nextgis_a STATIC ${AUTH_NEXTGIS_SRCS} ${AUTH_NEXTGIS_HDRS} ${AUTH_NEXTGIS_UIS_H})

target_include_directories(authmethod_nextgis_a  PRIVATE
    ${NGSTD_INCLUDE_DIRS}
)

target_include_directories(authmethod_nextgis_a PUBLIC ${CMAKE_SOURCE_DIR}/src/auth/nextgis/core)

# require c++17
target_compile_features(authmethod_nextgis_a PRIVATE cxx_std_17)

target_link_libraries(authmethod_nextgis_a qgis_core)

if (WITH_GUI)
  target_include_directories(authmethod_nextgis_a PRIVATE
    ${CMAKE_SOURCE_DIR}/src/auth/nextgis/gui
    ${CMAKE_BINARY_DIR}/src/auth/nextgis
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_BINARY_DIR}/src/app
  )

  target_link_libraries (authmethod_nextgis_a qgis_gui app)
endif()

target_compile_definitions(authmethod_nextgis_a PRIVATE "-DQT_NO_FOREACH")



if (FORCE_STATIC_LIBS)
  # for (external) mobile apps to be able to pick up provider for linking
  install (TARGETS authmethod_nextgis_a ARCHIVE DESTINATION ${QGIS_PLUGIN_DIR})
else()
  # dynamically loaded module
  add_library(authmethod_nextgis MODULE ${AUTH_NEXTGIS_SRCS} ${AUTH_NEXTGIS_HDRS} ${AUTH_NEXTGIS_UIS_H})

  target_include_directories(authmethod_nextgis  PRIVATE
      ${NGSTD_INCLUDE_DIRS}
  )

  # require c++17
  target_compile_features(authmethod_nextgis PRIVATE cxx_std_17)

  target_link_libraries(authmethod_nextgis qgis_core)

  if (WITH_GUI)
    target_include_directories(authmethod_nextgis PRIVATE
      ${CMAKE_SOURCE_DIR}/src/auth/nextgis/gui
      ${CMAKE_BINARY_DIR}/src/auth/nextgis
      ${CMAKE_SOURCE_DIR}/src/app
      ${CMAKE_BINARY_DIR}/src/app
    )
    target_link_libraries (authmethod_nextgis qgis_gui qgis_app)
    add_dependencies(authmethod_nextgis ui)
  endif()

  target_compile_definitions(authmethod_nextgis PRIVATE "-DQT_NO_FOREACH")

  install (TARGETS authmethod_nextgis
    RUNTIME DESTINATION ${QGIS_PLUGIN_DIR}
    LIBRARY DESTINATION ${QGIS_PLUGIN_DIR}
  )
endif()
