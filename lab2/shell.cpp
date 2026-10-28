#include<unistd.h>
#include<sys/types.h>
#include<iostream>
#include<vector>
#include<cstring>
#include<cassert>
#include<atomic>
#include<termios.h>
#include"pipe.hpp"
template<typename T>
struct shell_stack{
	shell_stack(size_t reserved=1){
		stk=new T[reserved];
		capacity=reserved;
	}
	T* data(){//Must be freed with delete[]
		T* ret=new T[length];
		memcpy(ret,stk,length*sizeof(T));
		return ret;
	}
	void push(T ch){
		if(length==capacity){
			capacity<<=1;
			T* ptr=new T[capacity];
			memcpy(ptr,stk,length*sizeof(T));
			delete[] stk;
			stk=ptr;
		}
		stk[length]=ch;
		++length;
	}
	void clear(){
		length=0;
	}
	void pop(){
		--length;
	}
	bool empty(){
		return length==0;
	}
	size_t size(){
		return length;
	}
	~shell_stack(){
		delete[] stk;
	}
private:
	T* stk;
	size_t length=0,capacity;
};
constexpr char DELIMITER=' ';
constexpr char TEXT_ENDIAN='\"';
constexpr char ESCAPE='\\';
shell_stack<char> str_stk;
shell_stack<char*> ptr_stk;
std::atomic<bool> interrupted=false;
int interactive;
pid_t shell_pid;
struct termios shell_tmodes;
extern struct sigaction SIGACTION_DEFAULT;
extern struct sigaction SIGACTION_IGNORE;
extern bool shell_waiting;
/*
if fd is -1, it's not a redirection
otherwise, check the return value, fd is the number before the symbol
0: < (input from file)
1: << (here document)
3: <<< (one line here document)
4: > (output to file)
6: >> (append to file);
*/
int shell_is_redirection(std::vector<char*>& input,size_t index,int& fd){
	fd=-1;
	if(index>=input.size()-1){
		return 0;
	}
	if(strcmp(input[index+1],"|")==0){
		return 0;
	}
	fd=0;//stdin
	char* ptr=input[index];
	size_t offset=0;
	while(ptr[offset]<='9'&&ptr[offset]>='0'){
		fd<<=1;
		fd+=fd<<2;
		fd+=ptr[offset]^'0';
		++offset;
	}
	if(strcmp(ptr+offset,"<")==0){
		return 0;
	}
	if(strcmp(ptr+offset,"<<")==0){
		return 1;
	}
	if(strcmp(ptr+offset,"<<<")==0){
		return 3;
	}
	if(!fd){
		fd=1;//stdout
	}
	if(strcmp(ptr+offset,">")==0){
		return 4;
	}
	if(strcmp(ptr+offset,">>")==0){
		return 6;
	}
	fd=-1;//invalid symbol
	return 0;
}
void shell_put_to_vector(std::vector<char*>& input){
	if(str_stk.empty()){
		return;
	}
	str_stk.push('\0');
	input.emplace_back(str_stk.data());
	str_stk.clear();
}
std::vector<char*> shell_command_input(void){
	std::vector<char*> result;
	bool match_string=false;//process ""
	bool escape=false;// process '\\'
	std::string command;
	if(std::getline(std::cin,command).fail()&&interrupted){
		std::cin.clear();
		return {};
	}
	for(auto&& i:command){
		if((i==DELIMITER)&&(!match_string)){//not in "" and meet space
			shell_put_to_vector(result);
			continue;
		}
		if(i==TEXT_ENDIAN){
			if(escape){
				str_stk.pop();//pop the '\'
				escape=false;
				goto put;
			}else{
				if(match_string){
					match_string=false;
					shell_put_to_vector(result);
					continue;
				}else{
					match_string=true;
					continue;
				}
			}
		}
		if(i==ESCAPE){
			if(escape){
				escape=false;//process '\\'
				continue;
			}else{
				escape=true;
			}
		}else{
			escape=false;
		}
		put:str_stk.push(i);
	}
	shell_put_to_vector(result);
	return result;
}
void shell_sigint_handler(int){
	char c='\n';
	write(1,&c,1);
	interrupted=true;
	shell_waiting=false;
}
int main(){
	//Init
	if(!isatty(0)){//This shell must be run in an interactive way
		return 1;
	}
	while(getpgrp()!=tcgetpgrp(0)){
		kill(-getpgrp(),SIGTTIN);
	}
	shell_pid=getpid();
	sigemptyset(&SIGACTION_IGNORE.sa_mask);
	sigemptyset(&SIGACTION_DEFAULT.sa_mask);
	std::cout<<"Current PID: "<<shell_pid<<std::endl;
	struct sigaction* sig=new struct sigaction;
	sig->sa_handler=shell_sigint_handler;
	sig->sa_flags=0;
	sigemptyset(&sig->sa_mask);
	shell_sigaction(SIGINT,sig);
	delete sig;
	shell_sigaction(SIGTTOU,&SIGACTION_IGNORE);
	shell_sigaction(SIGTTIN,&SIGACTION_IGNORE);
	shell_sigaction(SIGTSTP,&SIGACTION_IGNORE);
	shell_sigaction(SIGQUIT,&SIGACTION_IGNORE);
	if(setpgid(shell_pid,shell_pid)<0){
		perror("shell");
		return 1;
	}
	tcsetpgrp(0,shell_pid);
	tcgetattr(0, &shell_tmodes);
	std::cout.setf(std::ios::unitbuf);
	std::cerr.setf(std::ios::unitbuf);
	//REPL
	interrupt:while(std::cin){
		interrupted=false;
		shell_pipe_background_tick();
		std::cout<<"$";
		std::vector<char*> vec=shell_command_input();
		if(vec.empty()){
			continue;
		}
		bool is_command_first=true;
		int fd=-1,mode;
		shell_pipe_element element{};
		std::vector<shell_pipe_element> executions;
		for(size_t i=0;i<vec.size();++i){
			if(is_command_first){
				is_command_first=0;
				element.command=vec[i];
				goto psh;
			}
			mode=shell_is_redirection(vec,i,fd);
			if(fd>=0){//redirection
				if(mode==1){//Here document
					char* eof=vec[i+1];
					std::string str,res;
					if(std::getline(std::cin,str).fail()&&interrupted){
						std::cin.clear();
						for(auto&& i:vec){
							delete[] i;
						}
						for(auto&& i:executions){
							delete[] (i.parameters);
						}
						ptr_stk.clear();
						goto interrupt;
					}
					while(strcmp(str.c_str(),eof)){
						res+=str;
						res+='\n';
						if(std::getline(std::cin,str).fail()&&interrupted){
							std::cin.clear();
							for(auto&& i:vec){
								delete[] i;
							}
							for(auto&& i:executions){
								delete[] (i.parameters);
							}
							ptr_stk.clear();
							goto interrupt;
						}
					}
					if(res.size()){
						char* p=new char[res.size()];
						memcpy(p,res.c_str(),res.size());
						p[res.size()-1]='\0';
						vec[i+1]=p;
					}else{
						char* p=new char[1];
						p[0]='\0';
						vec[i+1]=p;
					}
					delete[] eof;
				}
				shell_redirection redirection;
				if(mode&1){
					redirection.type=shell_redirection_type::HEREDOC;
				}else{
					redirection.type=static_cast<shell_redirection_type>(mode>>1);
				}
				redirection.fd=fd;
				++i;//skip the next element as it's not a parameter of command
				redirection.source=vec[i];
				element.redirections.emplace_back(redirection);
				continue;
			}
			if(strcmp(vec[i],"|")==0){//pipe
				element.parameter_count=ptr_stk.size();
				ptr_stk.push(nullptr);
				element.parameters=ptr_stk.data();
				executions.emplace_back(element);
				element={};
				ptr_stk.clear();
				is_command_first=true;
				continue;
			}
			psh:ptr_stk.push(vec[i]);
		}
		element.parameter_count=ptr_stk.size();
		ptr_stk.push(nullptr);
		element.parameters=ptr_stk.data();
		executions.emplace_back(element);
		element={};
		ptr_stk.clear();
		shell_pipe_pipeline_run(executions);
	}
	return 0;
}
