#pragma once
#ifndef COMMAND_HPP_INCLUDED
#define COMMAND_HPP_INCLUDED
#include"pipe.hpp"
#include<utility>
/*
* Execute a command, 3 parameters:
* the first parameter is what should be done in parent process
* the second parameter is what should be done in child process
*/
std::pair<void(*)(shell_pipe_element),void(*)(shell_pipe_element)> shell_execute_command(shell_pipe_element);
#endif
