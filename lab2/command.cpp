#include"command.hpp"
#include<unistd.h>
#include<sys/wait.h>
#include<pwd.h>
#include<filesystem>
#include<system_error>
#include<iostream>
#include<cstring>
#include<map>
#include<termios.h>
#include<grp.h>
#include<string>
const std::map<std::string,int> SHELL_SIGNALS{
	{"SIGHUP",SIGHUP},
	{"SIGINT",SIGINT},
	{"SIGQUIT",SIGQUIT},
	{"SIGILL",SIGILL},
	{"SIGTRAP",SIGTRAP},
	{"SIGABRT",SIGABRT},
	{"SIGIOT",SIGIOT},
	{"SIGBUS",SIGBUS},
	{"SIGFPE",SIGFPE},
	{"SIGKILL",SIGKILL},
	{"SIGUSR1",SIGUSR1},
	{"SIGSEGV",SIGSEGV},
	{"SIGUSR2",SIGUSR2},
	{"SIGPIPE",SIGPIPE},
	{"SIGALRM",SIGALRM},
	{"SIGTERM",SIGTERM},
	{"SIGSTKFLT",SIGSTKFLT},
	{"SIGCHLD",SIGCHLD},
	{"SIGCONT",SIGCONT},
	{"SIGSTOP",SIGSTOP},
	{"SIGTSTP",SIGTSTP},
	{"SIGTTIN",SIGTTIN},
	{"SIGTTOU",SIGTTOU},
	{"SIGURG",SIGURG},
	{"SIGXCPU",SIGXCPU},
	{"SIGXFSZ",SIGXFSZ},
	{"SIGVTALRM",SIGVTALRM},
	{"SIGPROF",SIGPROF},
	{"SIGWINCH",SIGWINCH},
	{"SIGPOLL",SIGPOLL},
	{"SIGPWR",SIGPWR},
	{"SIGSYS",SIGSYS}
};
struct passwd* pw = getpwuid(getuid());
static std::filesystem::path shell_home=pw->pw_dir;
static std::filesystem::path shell_last_position="";
extern bool shell_waiting;
extern std::map<int,shell_wait_pgid_struct> background_process_groups;
extern pid_t shell_pid;
extern int count;
extern struct termios shell_tmodes;
void shell_noop_parent(shell_pipe_element){}
void shell_noop_child(shell_pipe_element){
	_exit(0);
}
void shell_external_command(shell_pipe_element element){//child
	execvp(element.command,element.parameters);
	perror("shell");
	_exit(1);
}
void shell_builtin_command_pwd(shell_pipe_element element){//child
	if(element.parameter_count>1){
		std::cerr<<"pwd: Too many parameters";
		_exit(1);
	}
	std::error_code code;
	std::filesystem::path p=std::filesystem::current_path(code);
	if(code){
		std::cerr<<"shell: "<<code.message()<<"\n";
	}else{
		std::cout<<p.string()<<"\n";
	}
	_exit(0);
}
void shell_builtin_command_cd(shell_pipe_element element){//parent
	if(element.parameter_count>2){
		std::cerr<<"cd: Too many parameters\n";
		return;
	}
	std::error_code code;
	std::filesystem::path p0=std::filesystem::current_path(code);
	std::filesystem::path p;
	if(element.parameter_count>1){
		p=(strcmp(element.parameters[1],"-")==0)?shell_last_position:element.parameters[1];
	}else{
		p=shell_home;
	}
	std::filesystem::current_path(p,code);
	if(code){
		std::cerr<<"shell: "<<code.message()<<": "<<p<<"\n";
		return;
	}
	shell_last_position=std::move(p0);
}
void shell_builtin_command_exit(shell_pipe_element element){//parent
	if(element.parameter_count>2){
		std::cerr<<"exit: Too many parameters\n";
		return;
	}
	if(element.parameter_count==1){
		exit(0);
	}
	exit(atoi(element.parameters[1]));
}
void shell_builtin_command_wait(shell_pipe_element element){//parent
	if(element.parameter_count>1){
		std::cerr<<"wait: Too many parameters\n";
		return;
	}
	shell_waiting=true;
}
void shell_builtin_command_jobs(shell_pipe_element element){//child
	if(element.parameter_count>1){
		std::cerr<<"jobs: Too many parameters\n";
		_exit(1);
	}
	for(auto&& [p,q]:background_process_groups){
		std::cout<<p<<", "<<q.pgid<<"\n";
	}
	_exit(0);
}
void shell_builtin_command_bg(shell_pipe_element element){//parent
	if(element.parameter_count!=2){
		std::cerr<<"bg: Too many or few parameter(s)\n";
		return;
	}
	pid_t p=0;
	for(size_t i=0;element.parameters[1][i]!='\0';++i){
		p*=10;
		p+=element.parameters[1][i]-'0';
	}
	if(background_process_groups.count(p)){
		kill(-p,SIGCONT);
	}else{
		std::cerr<<"bg: Invalid pgid: "<<p<<"\n";
	}
}
void shell_builtin_command_fg(shell_pipe_element element){//parent
	if(element.parameter_count!=2){
		std::cerr<<"fg: Too many or few parameter(s)\n";
		return;
	}
	int p=0;
	for(size_t i=0;element.parameters[1][i]!='\0';++i){
		p*=10;
		p+=element.parameters[1][i]-'0';
	}
	if(background_process_groups.count(p)){
		shell_wait_pgid_struct wait_pgid=background_process_groups[p];
		background_process_groups.erase(p);
		p=wait_pgid.pgid;
		tcsetpgrp(0,p);
		kill(-p,SIGCONT);
		wait_pgid.active_processes=wait_pgid.alive_processes;
		for(auto&& [m,n]:wait_pgid.process_status){
			auto&& [alive,active]=n;
			active=alive;
		}
		int status;
		while(wait_pgid.active_processes){
			pid_t wpid=waitpid(-p,&status,WUNTRACED);
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
			background_process_groups[wait_pgid.job_id]=wait_pgid;
			std::cerr<<"Pipeline "<<p<<" was put in background with job id "<<wait_pgid.job_id<<"\n";
		}
		tcsetpgrp(0,shell_pid);
		tcsetattr(0, TCSADRAIN, &shell_tmodes);
	}else{
		std::cerr<<"fg: Invalid job id: "<<p<<"\n";
	}
}
void shell_command_switch_user(char* user){
	struct passwd* pwd=getpwnam(user);
	if(pwd==nullptr){
		perror("username: ");
		return;
	}
	if(setegid(pwd->pw_gid)){
		perror("set group id");
	}
	if(seteuid(pwd->pw_uid)){
		perror("set user id");
	}
	pw=pwd;
	shell_home=pw->pw_dir;
}
void shell_builtin_command_su(shell_pipe_element element){//child
	if(element.parameter_count>2){
		std::cerr<<"su: Too many parameters\n";
		_exit(1);
	}
	if(element.parameter_count==1){//root
		shell_command_switch_user(const_cast<char*>("root"));
	}else{
		shell_command_switch_user(element.parameters[1]);
	}
	setenv("USER", pw->pw_name, 1);
	setenv("HOME", pw->pw_dir, 1);
	setenv("SHELL", pw->pw_shell, 1);
	if (initgroups(pw->pw_name, pw->pw_gid) == -1) {
		perror("su: ");
		_exit(1);
	}
	if(setgid(pw->pw_gid)<0){
		perror("su: ");
		_exit(1);
	}
	if(setuid(pw->pw_uid)<0){
		perror("su: ");
		_exit(1);
	}
	char** c=new char*[2];
	c[0]=pw->pw_shell;
	c[1]=nullptr;
	execvp(pw->pw_shell,c);
	perror("su: ");
}
void shell_builtin_command_sudo_parent(shell_pipe_element element){
	if(element.parameter_count==1){
		std::cerr<<"sudo: Null command\n";
		return;
	}
	element.command=element.parameters[1];
	++element.parameters;
	--element.parameter_count;
	size_t u_len=strlen(pw->pw_name);
	char* pu=new char[u_len+1];
	memcpy(pu,pw->pw_name,u_len+1);
	shell_command_switch_user(const_cast<char*>("root"));
	auto&& [p,c]=shell_execute_command(element);
	p(element);
	shell_command_switch_user(pu);
	delete[] pu;
}
void shell_builtin_command_sudo_child(shell_pipe_element element){
	if(element.parameter_count==1){
		_exit(1);
	}
	element.command=element.parameters[1];
	++element.parameters;
	--element.parameter_count;
	auto&& [p,c]=shell_execute_command(element);
	shell_command_switch_user(const_cast<char*>("root"));
	c(element);
}
void shell_builtin_command_kill(shell_pipe_element element){//parent
	if(element.parameter_count!=3){
		std::cerr<<"kill: Too many or too few parameters\n";
		return;
	}
	pid_t pid=std::atoi(element.parameters[1]);
	if(pid==0){
		std::cerr<<"kill: bad PID\n";
	}
	int sig;
	if(SHELL_SIGNALS.count(element.parameters[2])){//names like SIGINT
		sig=SHELL_SIGNALS.at(element.parameters[2]);
	}else{//number
		sig=std::atoi(element.parameters[2]);
	}
	if(kill(pid,sig)<0){
		perror("kill: ");
	}
}
std::pair<void(*)(shell_pipe_element),void(*)(shell_pipe_element)> shell_execute_command(shell_pipe_element element){
	if(std::strcmp(element.command,"pwd")==0){
		return {shell_noop_parent,shell_builtin_command_pwd};
	}
	if(std::strcmp(element.command,"cd")==0){
		return {shell_builtin_command_cd,shell_noop_child};
	}
	if(std::strcmp(element.command,"exit")==0){
		return {shell_builtin_command_exit,shell_noop_child};
	}
	if(std::strcmp(element.command,"wait")==0){
		return {shell_builtin_command_wait,shell_noop_child};
	}
	if(std::strcmp(element.command,"jobs")==0){
		return {shell_noop_parent,shell_builtin_command_jobs};
	}
	if(std::strcmp(element.command,"fg")==0){
		return {shell_builtin_command_fg,shell_noop_child};
	}
	if(std::strcmp(element.command,"bg")==0){
		return {shell_builtin_command_bg,shell_noop_child};
	}
	if(std::strcmp(element.command,"su")==0){
		return {shell_noop_parent,shell_builtin_command_su};
	}
	if(std::strcmp(element.command,"kill")==0){
		return {shell_builtin_command_kill,shell_noop_child};
	}
	return {shell_noop_parent,shell_external_command};
}
