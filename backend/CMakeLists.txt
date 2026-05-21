cmake_minimum_required(VERSION 3.20)

project(
    chatroom_backend
    VERSION 0.1.0
    DESCRIPTION "C++ high concurrency chatroom backend"
    LANGUAGES CXX
)

set(CHATROOM_BACKEND_TARGET chatroom_backend)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

find_path(MySQL_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql mariadb
)

find_library(MySQL_LIBRARY
    NAMES mysqlclient mariadb
)

find_path(Hiredis_INCLUDE_DIR
    NAMES hiredis/hiredis.h hiredis.h
)

find_library(Hiredis_LIBRARY
    NAMES hiredis
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(${CHATROOM_BACKEND_TARGET}
    src/main.cpp
    src/config/database_config.cpp
    src/config/redis_config.cpp
    src/message/message_bus.cpp
    src/message/message_repository.cpp
    src/server/websocket_server.cpp
)

target_compile_features(${CHATROOM_BACKEND_TARGET} PRIVATE cxx_std_20)

set_target_properties(${CHATROOM_BACKEND_TARGET}
    PROPERTIES
        CXX_EXTENSIONS OFF
)

target_include_directories(${CHATROOM_BACKEND_TARGET}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(${CHATROOM_BACKEND_TARGET}
    PRIVATE
        Threads::Threads
)

if(MySQL_INCLUDE_DIR AND MySQL_LIBRARY)
    target_compile_definitions(${CHATROOM_BACKEND_TARGET} PRIVATE CHATROOM_ENABLE_MYSQL)
    target_include_directories(${CHATROOM_BACKEND_TARGET} PRIVATE ${MySQL_INCLUDE_DIR})
    target_link_libraries(${CHATROOM_BACKEND_TARGET} PRIVATE ${MySQL_LIBRARY})
    message(STATUS "MySQL persistence enabled")
else()
    message(WARNING "MySQL client headers/library not found; backend will compile without persistence")
endif()

if(Hiredis_INCLUDE_DIR AND Hiredis_LIBRARY)
    target_compile_definitions(${CHATROOM_BACKEND_TARGET} PRIVATE CHATROOM_ENABLE_REDIS)
    target_include_directories(${CHATROOM_BACKEND_TARGET} PRIVATE ${Hiredis_INCLUDE_DIR})
    target_link_libraries(${CHATROOM_BACKEND_TARGET} PRIVATE ${Hiredis_LIBRARY})
    message(STATUS "Redis Pub/Sub enabled")
else()
    message(WARNING "hiredis headers/library not found; backend will compile without Redis Pub/Sub")
endif()

if(MSVC)
    target_compile_options(${CHATROOM_BACKEND_TARGET} PRIVATE /W4)
else()
    target_compile_options(${CHATROOM_BACKEND_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
endif()
