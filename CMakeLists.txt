# telltale — ESP-IDF component registration.
#
# The portable core is compiled into the same component rather than split out:
# it has no dependencies, and one component is simpler to consume. The host
# test suite compiles those same sources with plain gcc, so the code that
# formats metrics is verified on a laptop before it ever reaches a device.

idf_component_register(
    SRCS
        "src/telltale.c"
        "portable/src/telltale_core.c"
    INCLUDE_DIRS
        "include"
        "portable/include"
    REQUIRES
        esp_timer
        esp_event
    PRIV_REQUIRES
        esp_hw_support
        esp_system
        esp_app_format
        nvs_flash
        esp_wifi
        esp_http_server
        esp_http_client
)

# The library is warning-clean and intended to stay that way. These are the
# warnings that actually catch bugs in this kind of code: implicit conversions
# that silently truncate a size_t, and missing prototypes that hide a typo.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wshadow
    -Wpointer-arith
    -Wstrict-prototypes
    -Wmissing-prototypes
)
