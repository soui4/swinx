# Generate fccase.h from CaseFolding.txt

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fccase.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fc-case.py
            ${CMAKE_CURRENT_SOURCE_DIR}/CaseFolding.txt
            --template ${CMAKE_CURRENT_SOURCE_DIR}/fccase.tmpl.h
            --output ${CMAKE_CURRENT_BINARY_DIR}/fccase.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/CaseFolding.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/fccase.tmpl.h
            ${CMAKE_CURRENT_SOURCE_DIR}/fc-case.py
    COMMENT "Generating fccase.h"
)

# Create a custom target to ensure the header is generated
add_custom_target(fccase_h DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fccase.h)