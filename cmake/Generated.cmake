# Generated headers (docs/ui/naming.md section 6): data/ui/*.json -> <build>/generated/*.h.
# Regenerated only when an input or a generator changes; never written into the source tree.
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)

set(CC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(CC_DATA_UI "${CMAKE_SOURCE_DIR}/data/ui")

set(CC_GEN_TOKENS_OUT "${CC_GENERATED_DIR}/tokens.h")
add_custom_command(
    OUTPUT ${CC_GEN_TOKENS_OUT}
    COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/gen_tokens.py"
            --dark "${CC_DATA_UI}/theme-dark.json" --light "${CC_DATA_UI}/theme-light.json" --out "${CC_GENERATED_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/gen_tokens.py" "${CC_DATA_UI}/theme-dark.json" "${CC_DATA_UI}/theme-light.json"
    COMMENT "gen_tokens.py -> generated/tokens.h"
    VERBATIM
)

set(CC_GEN_REGISTRY_OUT
    "${CC_GENERATED_DIR}/actionids.h"
    "${CC_GENERATED_DIR}/panelids.h"
    "${CC_GENERATED_DIR}/settingkeys.h"
    "${CC_GENERATED_DIR}/workspaceids.h"
)
add_custom_command(
    OUTPUT ${CC_GEN_REGISTRY_OUT}
    COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/gen_registry.py"
            --registry "${CC_DATA_UI}/registry.json" --out "${CC_GENERATED_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/gen_registry.py" "${CC_DATA_UI}/registry.json"
    COMMENT "gen_registry.py -> generated/{actionids,panelids,settingkeys,workspaceids}.h"
    VERBATIM
)

add_custom_target(cc_generate DEPENDS ${CC_GEN_TOKENS_OUT} ${CC_GEN_REGISTRY_OUT})

# Link cc_generated to get the include path and the generation dependency.
add_library(cc_generated INTERFACE)
target_include_directories(cc_generated INTERFACE "${CMAKE_BINARY_DIR}")
add_dependencies(cc_generated cc_generate)
