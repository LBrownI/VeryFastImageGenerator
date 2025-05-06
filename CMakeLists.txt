cmake_minimum_required(VERSION 3.16) # Increased for better C++17 and filesystem support
project(RandomImageGenerator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF) # Prefer not to use GNU extensions

# Find OpenCV package
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR "OpenCV not found!")
endif()
include_directories(${OpenCV_INCLUDE_DIRS})

# Find Threads (for pthreads)
find_package(Threads REQUIRED)


# Add the executable
add_executable(random_image_generator generator.cpp)

# Link libraries
target_link_libraries(random_image_generator
    PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads # Modern CMake way to link pthreads

)

# For std::filesystem:
# With C++17 and modern compilers/linkers, this is often handled automatically.
# However, on some older systems or specific GCC versions (like GCC < 9),
# you might need to explicitly link against stdc++fs.
# For Clang, it might be c++fs.
# Example (uncomment if you face linking errors related to std::filesystem):
# if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
#   target_link_libraries(random_image_generator PRIVATE stdc++fs)
# elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#   # Check Clang version or try linking c++fs if needed
#   # target_link_libraries(random_image_generator PRIVATE c++fs)
# endif()

set_target_properties(random_image_generator PROPERTIES
    CXX_STANDARD ${CMAKE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ${CMAKE_CXX_STANDARD_REQUIRED}
    CXX_EXTENSIONS ${CMAKE_CXX_EXTENSIONS}
)

# Installation (optional, for installing the executable)
# install(TARGETS random_image_generator DESTINATION bin)
