set(COMPILER_FLAGS
    -Wall
    -Wcast-align
    -Wdouble-promotion
    -Wextra
    -Werror
    -Wformat=2
    -Wimplicit-fallthrough
    -Wmisleading-indentation
    -Wno-narrowing
    -Wno-deprecated-declarations
    -Wno-dangling-else
    -Wno-unused-but-set-variable
    -Wnon-virtual-dtor
    -Wnull-dereference
    -Wold-style-cast
    -pedantic
    -Wpedantic
    -Wunused
    # FIXME: The following bunch are needed for gcc12 atm.
    -Wno-missing-requires
    -Wno-restrict
    -Wno-null-dereference
    -Wno-maybe-uninitialized
    -Wno-unknown-warning-option # and this to work with clang
    # TODO: Address these and others in https://github.com/XRPLF/clio/issues/1273
)

# TODO: reenable when we change CI #884 if (is_gcc AND NOT lint) list(APPEND COMPILER_FLAGS -Wduplicated-branches
# -Wduplicated-cond -Wlogical-op -Wuseless-cast ) endif ()

if (is_clang)
  list(APPEND COMPILER_FLAGS -Wshadow # gcc is to aggressive with shadowing
                                      # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=78147
  )
endif ()

if (is_appleclang)
  list(APPEND COMPILER_FLAGS -Wreorder-init-list)
endif ()

if (san)
  # for the time being we want the builds to succeed and sanitizers to assess our runtime behaviour
  list(
    APPEND
    COMPILER_FLAGS
    -Wno-error=tsan # Disables treating TSAN warnings as errors
    -Wno-tsan # Disables TSAN warnings (thread-safety analysis)
    -Wno-uninitialized # Disables warnings about uninitialized variables (AddressSanitizer, UndefinedBehaviorSanitizer,
                       # etc.)
    -Wno-stringop-overflow # Disables warnings about potential string operation overflows (AddressSanitizer)
    -Wno-unsafe-buffer-usage # Disables warnings about unsafe memory operations (AddressSanitizer)
    -Wno-frame-larger-than # Disables warnings about stack frame size being too large (AddressSanitizer)
    -Wno-unused-function # Disables warnings about unused functions (LeakSanitizer, memory-related issues)
    -Wno-unused-but-set-variable # Disables warnings about unused variables (MemorySanitizer)
    -Wno-memset-zero # Disables warnings about unnecessary memset to zero (MemorySanitizer)
    -Wno-mutex # Disables warnings related to mutex usage (ThreadSanitizer)
    -Wno-sign-compare # Disables warnings about signed/unsigned comparison (UndefinedBehaviorSanitizer)
    -Wno-nonnull # Disables warnings related to null pointer dereferencing (UndefinedBehaviorSanitizer)
    -Wno-address # Disables warnings about address-related issues (UndefinedBehaviorSanitizer)
  )
endif ()

# See https://github.com/cpp-best-practices/cppbestpractices/blob/master/02-Use_the_Tools_Available.md#gcc--clang for
# the flags description

target_compile_options(clio_options INTERFACE ${COMPILER_FLAGS})
