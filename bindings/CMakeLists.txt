# Language bindings. The C ABI is compiled into zui; Python and Rust call that
# ABI and are not separate CMake targets. See bindings/README.md.
add_subdirectory(c)
