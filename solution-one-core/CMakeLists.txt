# GENERATED for matmul/secret-matrix-batches@1.0.0.
cmake_minimum_required(VERSION 3.16)
project(solution CXX)

find_package(OpenFHE REQUIRED)

# The library's own flags carry the ABI-relevant defines (MATHBACKEND); a
# solution built without them links and then miscomputes.
set(CMAKE_CXX_STANDARD ${OpenFHE_CXX_STANDARD})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenFHE_CXX_FLAGS}")

include_directories(${OpenFHE_INCLUDE} ${OpenFHE_INCLUDE}/third-party/include
                    ${OpenFHE_INCLUDE}/core ${OpenFHE_INCLUDE}/pke
                    ${OpenFHE_INCLUDE}/binfhe)
link_directories(${OpenFHE_LIBDIR})

add_executable(solution main.cpp solve.cpp)
target_link_libraries(solution ${OpenFHE_SHARED_LIBRARIES})
