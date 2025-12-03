# SPDX-FileCopyrightText: 2019 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Gets a UTC timestamp and sets the provided variable to it
function(get_timestamp _var)
    string(TIMESTAMP timestamp UTC)
    set(${_var} "${timestamp}" PARENT_SCOPE)
endfunction()

# generate git/build information
include(GetGitRevisionDescription)
if(NOT GIT_REF_SPEC)
    get_git_head_revision(GIT_REF_SPEC GIT_REV)
endif()
if(NOT GIT_DESC)
    git_describe(GIT_DESC --always --long --dirty)
endif()
if (NOT GIT_BRANCH)
  git_branch_name(GIT_BRANCH)
endif()
get_timestamp(BUILD_DATE)

#
# Generate build information based on the CITRON_BUILD_TYPE variable.
#
if(CITRON_BUILD_TYPE STREQUAL "Stable")
    # For Stable builds, use a clean version string and an empty build name for the title bar.
    set(BUILD_FULLNAME "")
    # These variables are set in the root CMakeLists.txt, but we provide a fallback.
    if(DEFINED CITRON_VERSION_MAJOR)
        set(BUILD_VERSION "${CITRON_VERSION_MAJOR}.${CITRON_VERSION_MINOR}.${CITRON_VERSION_PATCH}")
    else()
        set(BUILD_VERSION "0.11.0") # Fallback stable version
    endif()
    set(BUILD_ID ${DISPLAY_VERSION})

else()
    # For Nightly or local developer builds, use the existing logic to generate a name and use the git hash.
    set(REPO_NAME "")
    set(BUILD_VERSION "0")
    set(BUILD_ID ${DISPLAY_VERSION})
    if (BUILD_REPOSITORY)
      # regex capture the string nightly or canary into CMAKE_MATCH_1
      string(REGEX MATCH "citron-emu/citron-?(.*)" OUTVAR ${BUILD_REPOSITORY})
      if ("${CMAKE_MATCH_COUNT}" GREATER 0)
        # capitalize the first letter of each word in the repo name.
        string(REPLACE "-" ";" REPO_NAME_LIST ${CMAKE_MATCH_1})
        foreach(WORD ${REPO_NAME_LIST})
          string(SUBSTRING ${WORD} 0 1 FIRST_LETTER)
          string(SUBSTRING ${WORD} 1 -1 REMAINDER)
          string(TOUPPER ${FIRST_LETTER} FIRST_LETTER)
          set(REPO_NAME "${REPO_NAME}${FIRST_LETTER}${REMAINDER}")
        endforeach()
        if (BUILD_TAG)
          string(REGEX MATCH "${CMAKE_MATCH_1}-([0-9]+)" OUTVAR ${BUILD_TAG})
          if (${CMAKE_MATCH_COUNT} GREATER 0)
            set(BUILD_VERSION ${CMAKE_MATCH_1})
          endif()
          if (BUILD_VERSION)
            # This leaves a trailing space on the last word, but we actually want that
            # because of how it's styled in the title bar.
            set(BUILD_FULLNAME "${REPO_NAME} ${BUILD_VERSION} ")
          else()
            set(BUILD_FULLNAME "")
          endif()
        endif()
      endif()
    endif()

    # Add a fallback for local builds
    # If BUILD_VERSION is still "0", it means the CI-specific logic was not executed.
    # In that case, we should use the git description which contains the commit hash.
    if (BUILD_VERSION STREQUAL "0")
        set(BUILD_FULLNAME "Nightly ") # Ensure local builds are also identified as Nightly
        set(BUILD_VERSION ${GIT_DESC})
    endif()
endif()

configure_file(scm_rev.cpp.in scm_rev.cpp @ONLY)
