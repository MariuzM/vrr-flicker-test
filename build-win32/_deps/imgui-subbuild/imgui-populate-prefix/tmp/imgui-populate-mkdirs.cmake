# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-src"
  "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-build"
  "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-subbuild/imgui-populate-prefix"
  "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-subbuild/imgui-populate-prefix/tmp"
  "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-subbuild/imgui-populate-prefix/src/imgui-populate-stamp"
  "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-subbuild/imgui-populate-prefix/src"
  "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-subbuild/imgui-populate-prefix/src/imgui-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-subbuild/imgui-populate-prefix/src/imgui-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Volumes/Media/Dev/Projects/project__vrr-flicker/build-win32/_deps/imgui-subbuild/imgui-populate-prefix/src/imgui-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
