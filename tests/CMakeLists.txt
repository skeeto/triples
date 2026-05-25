add_executable(game_tests
    test_main.cpp
    test_deck.cpp
    test_move.cpp
    test_bonus.cpp
    test_score.cpp
    test_serialize.cpp
    ${CMAKE_SOURCE_DIR}/src/game/board.cpp
    ${CMAKE_SOURCE_DIR}/src/game/deck.cpp
    ${CMAKE_SOURCE_DIR}/src/game/move.cpp
    ${CMAKE_SOURCE_DIR}/src/game/rng.cpp
    ${CMAKE_SOURCE_DIR}/src/game/score.cpp
    ${CMAKE_SOURCE_DIR}/src/game/game_state.cpp
    ${CMAKE_SOURCE_DIR}/src/game/serialize.cpp
)
target_include_directories(game_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME game_tests COMMAND game_tests)
