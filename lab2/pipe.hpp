#pragma once
#ifndef PIPE_HPP_INCLUDED
#define PIPE_HPP_INCLUDED
#include<vector>
#include<map>
#include<utility>
#include<signal.h>
enum struct shell_redirection_type: int{
	INPUT=0,HEREDOC=1,WRITE=2,APPEND=3
};
struct shell_redirection{
	shell_redirection_type type;
	int fd;
	char* source;
};
struct shell_pipe_element{
	char* command;
	char** parameters;
	size_t parameter_count;
	std::vector<shell_redirection> redirections;
	void child_redirection();
};
struct shell_wait_pgid_struct{
	pid_t pgid;
	int job_id;
	size_t alive_processes;
	size_t active_processes;
	std::map<pid_t,std::pair<bool,bool>> process_status;//{alive,active}
	bool operator <(const shell_wait_pgid_struct& rhs) const{
		return pgid<rhs.pgid;
	}
};
void shell_pipe_background_tick();//Used to avoid zombie
void shell_pipe_pipeline_run(std::vector<shell_pipe_element>& pipeline);
void shell_sigaction(int,struct sigaction*);
#endif
