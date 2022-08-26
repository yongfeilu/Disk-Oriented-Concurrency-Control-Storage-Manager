# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-src"
  "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-build"
  "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-download/googletest-prefix"
  "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-download/googletest-prefix/tmp"
  "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-download/googletest-prefix/src/googletest-stamp"
  "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-download/googletest-prefix/src"
  "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-download/googletest-prefix/src/googletest-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/googletest-download/googletest-prefix/src/googletest-stamp/${subDir}")
endforeach()
