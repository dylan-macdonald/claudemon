set(SOURCE_FILES
	api-client.c
	ai-player.c)

add_library(mgba-claude OBJECT ${SOURCE_FILES})

# Find required dependencies
find_package(CURL REQUIRED)

target_link_libraries(mgba-claude PUBLIC CURL::libcurl)
target_include_directories(mgba-claude PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Expose headers
target_include_directories(mgba-claude PUBLIC
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/src
)
