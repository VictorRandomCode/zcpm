function(SetupCompiler OPTIONS)

    # Enable sanitisers of interest.
    if (USE_SANITISERS)
        message("Sanitisers are enabled for '${PROJECT_NAME}'")
        # Note that enabling these does slow down both compilation and execution.  But according
        # to the docco for these, we should usually keep these enabled in most cases, it's worth it.
        # However, they can clash with some tools e.g. valgrind, gdb, etc.
        add_compile_options(-fsanitize=address -fsanitize=undefined)
        add_link_options(-fsanitize=address -fsanitize=undefined)
    else()
        message("Sanitisers are not enabled for '${PROJECT_NAME}'")
    endif()

    # Enable profiling, which is supported only on GCC.
    if (USE_PROFILE)
        if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            message("Profiling is being enabled for '${PROJECT_NAME}'")
            add_compile_options(-pg)
            add_link_options(-pg)
        else()
            message("Profiling is only supported for GCC, not ${CMAKE_CXX_COMPILER_ID}")
        endif()
    else()
        message("Profiling is not enabled for '${PROJECT_NAME}'")
    endif()

endfunction()
