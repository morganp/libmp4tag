add_executable(test_mp4tag test_mp4tag.c)
target_link_libraries(test_mp4tag PRIVATE mp4tag)
add_test(NAME test_mp4tag COMMAND test_mp4tag)
