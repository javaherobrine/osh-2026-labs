#ifndef REQUEST_HPP_INCLUDED
#define REQUEST_HPP_INCLUDED
#include<map>
#include<string>
enum struct http_version{
	http_0_9=9, http_1_0=10, http_1_1=11, http_2_0=20
};
enum struct http_request_method{
	GET,POST,PUT,DELETE,HEAD,TRACE,OPTIONS,PATCH,CONNECT
};
struct http_request_header{
	http_version version;
	std::string resource="";
	http_request_method method;
	std::multimap<std::string,std::string> headers;
};
#endif
