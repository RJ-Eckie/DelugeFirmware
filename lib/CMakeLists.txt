cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG a97439fbed8620659180e60eea861e2bebcfcf70
)
FetchContent_MakeAvailable(argon)

