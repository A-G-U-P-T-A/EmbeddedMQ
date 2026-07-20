# Sanitizer support.
#   -DEMQ_ENABLE_SANITIZERS=ON          legacy: ASan+UBSan (non-MSVC)
#   -DEMQ_SANITIZE=address;undefined    Clang/GCC list
#   -DEMQ_SANITIZE=thread               ThreadSanitizer
#   -DEMQ_ASAN=ON                       MSVC /fsanitize=address

option(EMQ_ASAN "Enable MSVC AddressSanitizer" OFF)
set(EMQ_SANITIZE "" CACHE STRING "Clang/GCC sanitizers (address;undefined;thread)")

# Let tests skip RSS gates that sanitizer shadow memory would trip.
if(EMQ_SANITIZE OR EMQ_ENABLE_SANITIZERS OR EMQ_ASAN)
  add_compile_definitions(EMQ_SANITIZER_BUILD=1)
endif()

function(emq_apply_sanitizers target)
  if(MSVC)
    if(EMQ_ASAN)
      target_compile_options(${target} PRIVATE /fsanitize=address)
      target_link_options(${target} PRIVATE /fsanitize=address)
    endif()
    return()
  endif()

  set(_sans "")
  if(EMQ_SANITIZE)
    string(REPLACE ";" "," _sans "${EMQ_SANITIZE}")
  elseif(EMQ_ENABLE_SANITIZERS)
    set(_sans "address,undefined")
  endif()

  if(_sans)
    # PUBLIC so dependents inherit compile + link flags. A static lib alone
    # being instrumented is not enough — final links need -fsanitize= too.
    target_compile_options(${target} PUBLIC
      -fsanitize=${_sans} -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=${_sans})
  endif()
endfunction()
