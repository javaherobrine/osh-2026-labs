#ifndef THREAD_POOL_INCLUDED
#define THREAD_POOL_INCLUDED
#include<atomic>
#include<mutex>
#include<queue>
#include<array>
#include<thread>
#include<semaphore>
void httpserver_request_handler(int sockfd);
template<std::size_t N>
struct httpserver_fixed_threadpool{
	httpserver_fixed_threadpool(){
		for(auto&& i:array){
			i=std::thread{&httpserver_fixed_threadpool::thread_function,this};
		}
	}
	void push(int sockfd){
		if(sockfd==-1){
			return;
		}
		mutual_exclusion.lock();
		fds.push(sockfd);
		mutual_exclusion.unlock();
		sem.release();
	}
	void shutdown_now(){
		shutdown=true;
		sem.release(N);
	}
	void join(){
		for(auto&& i:array){
			i.join();
		}
	}
	operator bool(){
		return !shutdown;
	}
private:
	std::array<std::thread,N> array;
	std::atomic<bool> shutdown=false;
	std::mutex mutual_exclusion;
	std::queue<int> fds;
	std::counting_semaphore<> sem{0};
	void thread_function(){
		while(true){
			sem.acquire();
			mutual_exclusion.lock();
			if(fds.empty()){
				mutual_exclusion.unlock();
				if(shutdown){
					break;
				}
				continue;
			}
			int fd=fds.front();
			fds.pop();
			mutual_exclusion.unlock();
			httpserver_request_handler(fd);
		}
	}
};
#endif
