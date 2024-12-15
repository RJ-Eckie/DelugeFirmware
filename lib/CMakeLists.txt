cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG c19a93d3228d74782c1d17e6bc0b1acb74001aff
)
FetchContent_MakeAvailable(argon)

