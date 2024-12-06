list(APPEND ADD_INCLUDE "include"
                        )

list(APPEND ADD_SRCS "src/openeth.c"
                     )

if (CONFIG_UNIT_TEST_ENABLE_CODE_COVERAGE)
    list(APPEND ADD_DEFINITIONS -fno-profile-arcs -fno-test-coverage)
endif()

register_component()
