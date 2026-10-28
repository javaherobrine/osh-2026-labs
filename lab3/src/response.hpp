#ifndef RESPONSE_HPP_INCLUDED
#define RESPONSE_HPP_INCLUDED
#include<unordered_map>
#include<set>
#include<string>
#include<mutex>
#include<ctime>
#include<sys/stat.h>
#include<sys/socket.h>
#include<sys/sendfile.h>
#include<fcntl.h>
#include<netinet/tcp.h>
#include<netinet/in.h>
#include<unistd.h>
#include<shared_mutex>
#include<list>
#include<cstdio>
#include<sstream>
struct http_file_cache{
	char* data=nullptr;
	std::string filename;
	std::time_t last_modification=0;
	~http_file_cache();
	bool on_modify(const struct stat& status);
};
extern const std::string NOT_FOUND_HTML;
extern const std::string INTERNAL_ERROR_HTML;
extern const std::size_t NOT_FOUND_SIZE;
extern const std::size_t INTERNAL_ERROR_SIZE;
void http_write(int fd, const char* ptr, size_t length);
void http_response_header(int code, std::size_t length, std::string message, int sockfd);
template<std::size_t max_cached, std::size_t max_file_size>
struct http_response_with_cache{
	void response_file(std::string filename, int sockfd, int code, std::string message){
		bool option=true;
		setsockopt(sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
		struct stat status;
		if(stat(filename.c_str(),&status)){
			evict(filename);//The file is deleted but maybe it lives in the cache
			http_response_header(404,NOT_FOUND_SIZE,"Not Found",sockfd);
			http_write(sockfd,NOT_FOUND_HTML.c_str(),NOT_FOUND_SIZE);
			option=false;
			setsockopt(sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
			return;
		}
		if(S_ISDIR(status.st_mode)){
			http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",sockfd);
			http_write(sockfd,INTERNAL_ERROR_HTML.c_str(),INTERNAL_ERROR_SIZE);
			option=false;
			setsockopt(sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
			return;
		}
		http_response_header(200,status.st_size,"OK",sockfd);
		if(status.st_size<=max_file_size){
			http_file_cache* cached_file=cache(filename,status);
			if(cached_file==nullptr){
				http_sendfile(filename,sockfd,status);
			}else{
				http_write(sockfd,cached_file->data,status.st_size);
			}
		}else{
			http_sendfile(filename,sockfd,status);
		}
		option=false;
		setsockopt(sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
	}
private:
	std::mutex mutual_exclusion;
	std::list<http_file_cache*> cached;
	std::unordered_map<std::string,std::list<http_file_cache*>::iterator> mapped;
	http_file_cache* cache(const std::string& filename, const struct stat& status){
		std::lock_guard<std::mutex> lock{mutual_exclusion};//In Java, I ofteh use `synchronized` keyword
		std::unordered_map<std::string,std::list<http_file_cache*>::iterator>::iterator iter=mapped.find(filename);
		if(iter!=mapped.end()){
			cached.splice(cached.begin(),cached,iter->second);
			if(status.st_mtime!=(*(iter->second))->last_modification){
				if(!(*(iter->second))->on_modify(status)){//Fail to update, evict it
					evict_unsafe(filename);
					return nullptr;
				}
			}
			return *(iter->second);
		}
		http_file_cache* c=new http_file_cache;
		c->filename=filename;
		if(!(c->on_modify(status))){
			delete c;
			return nullptr;
		}
		cached.push_front(c);
		mapped[filename]=cached.begin();
		while(cached.size()>max_cached){//evict old caches
			evict_unsafe(cached.back()->filename);
		}
		return c;
	}
	void evict(const std::string& key){
		std::lock_guard<std::mutex> lock{mutual_exclusion};
		evict_unsafe(key);
	}
	void evict_unsafe(const std::string& key){
		std::unordered_map<std::string,std::list<http_file_cache*>::iterator>::iterator to_be_evicted=mapped.find(key);
		if(to_be_evicted==mapped.end()){//Not a valid cache
			return;
		}
		delete *(to_be_evicted->second);
		cached.erase(to_be_evicted->second);
		mapped.erase(to_be_evicted);
	}
	void http_sendfile(const std::string& filename, int sockfd, const struct stat& status){
		evict(filename);//Maybe the file is growing very large and shoule be removed from cache
		int fd=open(filename.c_str(),O_RDONLY);
		size_t off=0;
		while(off!=status.st_size){
			ssize_t s=sendfile(sockfd,fd,nullptr,status.st_size-off);
			if(s<0){
				break;
			}
			off+=s;
		}
		close(fd);
	}
};
#endif
