# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.23

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/local/Cellar/cmake/3.23.2/bin/cmake

# The command to remove a file.
RM = /usr/local/Cellar/cmake/3.23.2/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project"

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build"

# Utility rule file for check-clang-tidy.

# Include any custom commands dependencies for this target.
include CMakeFiles/check-clang-tidy.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/check-clang-tidy.dir/progress.make

CMakeFiles/check-clang-tidy:
	../build_support/run_clang_tidy.py -clang-tidy-binary /usr/local/opt/llvm@12/bin/clang-tidy -p /Users/luyongfei/Desktop/Supplement\ Courses/CMU15445/project/build

check-clang-tidy: CMakeFiles/check-clang-tidy
check-clang-tidy: CMakeFiles/check-clang-tidy.dir/build.make
.PHONY : check-clang-tidy

# Rule to build all files generated by this target.
CMakeFiles/check-clang-tidy.dir/build: check-clang-tidy
.PHONY : CMakeFiles/check-clang-tidy.dir/build

CMakeFiles/check-clang-tidy.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/check-clang-tidy.dir/cmake_clean.cmake
.PHONY : CMakeFiles/check-clang-tidy.dir/clean

CMakeFiles/check-clang-tidy.dir/depend:
	cd "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build" && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project" "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project" "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build" "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build" "/Users/luyongfei/Desktop/Supplement Courses/CMU15445/project/build/CMakeFiles/check-clang-tidy.dir/DependInfo.cmake" --color=$(COLOR)
.PHONY : CMakeFiles/check-clang-tidy.dir/depend

