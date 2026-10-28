#include<unistd.h>
#include<fcntl.h>
#include<cstring>
#include<sys/types.h>
#include<signal.h>
#include<sys/wait.h>
#include<vector>
#include<iostream>
#include<atomic>
#include<sys/mman.h>
#include<new>
#include<termios.h>
#include<map>
#include<set>
#include<pwd.h>
#include<grp.h>
#include"pipe.hpp"
#include"command.hpp"
constexpr size_t SHELL_SHARED_MEMORY_SIZE=sizeof(std::atomic<size_t>);
struct sigaction SIGACTION_DEFAULT{SIG_DFL,0,0,0,0};
struct sigaction SIGACTION_IGNORE{SIG_IGN,0,0,0,0};
std::map<int,shell_wait_pgid_struct> background_process_groups;
static std::set<pid_t> heredoc_processes;
int count=0;
extern pid_t shell_pid;
extern struct termios shell_tmodes;
extern struct passwd* pw;
bool shell_waiting=false;
void shell_pipe_background_tick(){
	int status;
	for(std::set<pid_t>::iterator iter=heredoc_processes.begin();iter!=heredoc_processes.end();){
		if(waitpid(*iter,&status,WNOHANG)>0&&(WIFEXITED(status)||WIFSIGNALED(status))){
			iter=heredoc_processes.erase(iter);
		}else{
			++iter;
		}
	}
	for(std::map<int,shell_wait_pgid_struct>::iterator iter=background_process_groups.begin();iter!=background_process_groups.end();){
		shell_wait_pgid_struct wait_pgid=iter->second;
		pid_t wpid=waitpid(-wait_pgid.pgid,&status,shell_waiting?0:WNOHANG);
		while(wpid){
			if(wpid>0){
				if(WIFEXITED(status) || WIFSIGNALED(status)){
					auto&& [alive,active]=wait_pgid.process_status[wpid];
					wait_pgid.alive_processes-=alive;
					alive=false;
					if(wait_pgid.alive_processes==0){
						std::cerr<<"background job "<<wait_pgid.job_id<<" done\n";
						iter=background_process_groups.erase(iter);
						break;
					}
				}
			}else{
				if(errno == EINTR){
					shell_waiting=false;
					return;
				}
				break;
			}
			wpid=waitpid(-wait_pgid.pgid,&status,shell_waiting?0:WNOHANG);
		}
		if(wait_pgid.alive_processes){
			iter->second=wait_pgid;
			++iter;
		}
	}
	shell_waiting=false;
}
void shell_close_pipe(int fd){
	if(close(fd)<0){
		std::cout<<"Warning: failed to close pipe, potential resource leak detected\n";
		perror("pipe close");
	}
}
int shell_open_file_read(char* file){
	int fd=open(file,O_RDONLY);
	if(fd<0){
		std::cout<<"Failed to create redirection\n";
		perror("open");
	}
	return fd;
}
int shell_open_file_write(char* file,bool append){
	int fd=open(file,O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC),0777);
	if(fd<0){
		std::cout<<"Failed to create redirection\n";
		perror("open");
	}
	return fd;
}
void shell_open_pipe(int p[2]){
	if(pipe(p)<0){
		std::cout<<"Error: pipeline creation\n";
		perror("pipe");
	}
}
void shell_dup2(int fd0,int fd1){
	if(dup2(fd0,fd1)<0){
		std::cout<<"Failed to redirect the I/O\n";
		perror("redirection");
	}
}
void shell_sigaction(int sig,struct sigaction* ptr){
	if(sigaction(sig,ptr,0)<0){
		perror("Signal: ");
	}
}
void shell_pipe_write_operation(char* content, int fd){
	int fds[2];
	if(pipe(fds)<0){
		perror("pipe creation");
		std::cout<<"Failed to create pipes\n";
		return;
	}
	pid_t p=fork();
	if(p==0){
		shell_close_pipe(fds[0]);//in child process, close read pipe
		size_t offset=0,length=strlen(content);
		while(offset!=length){
			ssize_t n=write(fds[1],content+offset,length-offset);
			if(n>=0){
				offset+=static_cast<size_t>(n);
			}else{
				perror("write");
				break;
			}
		}
		_exit(0);
	}else if(p>0){
		shell_dup2(fds[0],fd);//fd is redirected to the read pipe
		shell_close_pipe(fds[1]);//in parent process, close write pipe
		setpgid(p,p);
		heredoc_processes.insert(p);
		return;
	}else{
		perror("fork");
		shell_close_pipe(fds[0]);
		shell_close_pipe(fds[1]);
		std::cout<<"Error: unable to create child process\n";
	}
}
void shell_pipe_element::child_redirection(){
	if(setegid(0)==0){//prevent setuid(0) and other operations like this
		seteuid(0);
		if (initgroups(pw->pw_name, pw->pw_gid) == -1) {
        		perror("security: ");
        		std::cerr<<"To avoid your PC from risk, this command is refused\n";
			_exit(2);
		}
		if(setgid(pw->pw_gid) == -1){
			perror("security: ");
			std::cerr<<"To avoid your PC from risk, this command is refused\n";
			_exit(2);
		}
		if(setuid(pw->pw_uid) == -1){
			perror("security: ");
			std::cerr<<"To avoid your PC from risk, this command is refused\n";
			_exit(2);
		}
	}
	int fd1;
	for(auto&& [type,fd,content]:redirections){
		switch(type){
		case shell_redirection_type::INPUT:
			fd1=shell_open_file_read(content);
			if(fd1>=0){
				close(fd1);
				shell_dup2(fd1,fd);
			}
			break;
		case shell_redirection_type::HEREDOC:
			shell_pipe_write_operation(content,fd);
			break;
		case shell_redirection_type::WRITE:
		case shell_redirection_type::APPEND:
			fd1=shell_open_file_write(content,type==shell_redirection_type::APPEND);
			if(fd1>=0){
				close(fd1);
				shell_dup2(fd1,fd);
			}
		}
	}
	shell_sigaction(SIGINT,&SIGACTION_DEFAULT);
	shell_sigaction(SIGTTOU,&SIGACTION_DEFAULT);
	shell_sigaction(SIGTTIN,&SIGACTION_DEFAULT);
	shell_sigaction(SIGTSTP,&SIGACTION_DEFAULT);
	shell_sigaction(SIGQUIT,&SIGACTION_DEFAULT);
}
void shell_pipe_pipeline_run(std::vector<shell_pipe_element>& pipeline){
	if(pipeline.empty()){
		return;//No commands to be invoked
	}
	shell_pipe_element& e=pipeline.back();
	bool background=false;
	if(e.parameter_count>1&&strcmp(e.parameters[e.parameter_count-1],"&")==0){//If a pipeline ends with &, it's a background task
		background=true;
		char*& ptr=e.parameters[--e.parameter_count];
		delete[] ptr;
		e.parameters[e.parameter_count]=nullptr;
	}
	void* shared_mutex=mmap(0, SHELL_SHARED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if(shared_mutex==MAP_FAILED){
		perror("mutex: ");
		return;
	}
	shell_wait_pgid_struct wait_pgid;
	wait_pgid.alive_processes=pipeline.size();
	wait_pgid.active_processes=pipeline.size();
	pid_t pgid=0;
	std::atomic<size_t>* mutex_ptr=new (shared_mutex) std::atomic<size_t>;
	mutex_ptr[0]=1;
	if(pipeline.size()==1){//No pipeline
		pid_t pid=fork();
		pgid=pid;
		auto&& [p,c]=shell_execute_command(pipeline[0]);
		wait_pgid.process_status[pid]={true, true};
		if(pid<0){
			munmap(shared_mutex, SHELL_SHARED_MEMORY_SIZE);
			perror("fork");
			return;
		}else if(pid==0){
			setpgid(0, pgid);
			pipeline[0].child_redirection();
			while(mutex_ptr[0]);//parent process must set it to foreground or background before
			c(pipeline[0]);
		}else{
			p(pipeline[0]);
			setpgid(pid,pid);
		}
	}else{
		int fd0[2],fd1[2];
		for(size_t i=0;i<pipeline.size();++i){
			auto&& [p0,c]=shell_execute_command(pipeline[i]);
			if(i){
				if(i>1){
					shell_close_pipe(fd0[0]);
					shell_close_pipe(fd0[1]);
				}
				memcpy(fd0,fd1,8);
			}
			if(i!=pipeline.size()-1){
				shell_open_pipe(fd1);
			}
			pid_t p=fork();
			if(p==0){
				setpgid(0, pgid);
				pipeline[i].child_redirection();
				if(i){
					shell_dup2(fd0[0],0);
					shell_close_pipe(fd0[1]);
					shell_close_pipe(fd0[0]);
				}
				if(i!=pipeline.size()-1){
					shell_dup2(fd1[1],1);
					shell_close_pipe(fd1[0]);
					shell_close_pipe(fd1[1]);
				}
				while(mutex_ptr[0]);
				c(pipeline[i]);
			}else{
				p0(pipeline[i]);
				if(pgid==0){
					pgid=p;
				}
				wait_pgid.process_status[p]={true,true};
				setpgid(p,pgid);
			}
		}
		shell_close_pipe(fd0[0]);
		shell_close_pipe(fd0[1]);
	}
	wait_pgid.pgid=pgid;
	if(background){
		mutex_ptr[0]=false;
		munmap(shared_mutex,SHELL_SHARED_MEMORY_SIZE);
		wait_pgid.job_id=++count;
		wait_pgid.active_processes=0;
		std::cerr<<"Pipeline "<<pgid<<" was put in background with job id "<<wait_pgid.job_id<<"\n";
		background_process_groups[wait_pgid.job_id]=wait_pgid;
	}else{
		tcsetpgrp(0,pgid);
		mutex_ptr[0]=false;
		munmap(shared_mutex,SHELL_SHARED_MEMORY_SIZE);
		int status;
		while(wait_pgid.active_processes){
			pid_t wpid=waitpid(-pgid,&status,WUNTRACED);
			if(wpid>0){
				if(WIFEXITED(status) || WIFSIGNALED(status)){
					auto&& [alive,active]=wait_pgid.process_status[wpid];
					wait_pgid.alive_processes-=alive;
					wait_pgid.active_processes-=active;
					alive=false;
					active=false;
				}else if(WIFSTOPPED(status)){
					auto&& [alive,active]=wait_pgid.process_status[wpid];
					wait_pgid.active_processes-=active;
					active=false;
				}
			}else{
				if(errno == ECHILD){
					wait_pgid.alive_processes=0;
					wait_pgid.active_processes=0;
				}else if(errno == EINTR){
					continue;
				}
				break;
			}
		}
		if(wait_pgid.alive_processes){//put in background
			wait_pgid.job_id=++count;
			background_process_groups[wait_pgid.job_id]=wait_pgid;
			std::cerr<<"Pipeline "<<pgid<<" was put in background with job id "<<wait_pgid.job_id<<"\n";
		}
		tcsetpgrp(0,shell_pid);
		tcsetattr(0, TCSADRAIN, &shell_tmodes);
	}
	for(auto&& i:pipeline){
		for(size_t j=0;j<i.parameter_count;++j){
			delete[] i.parameters[j];
		}
		delete[] i.parameters;
	}
}
