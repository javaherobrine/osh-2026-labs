#include"string_split.hpp"
/*
The implementation of function split
Strings with no content inside are removed automatically
*/
std::vector<std::string> split(std::string s, const std::string& d){
	using namespace std;
	size_t dsize=d.size();
	if(dsize==0){
		return {s};//no split point for this case
	}
	//KMP Algorithm compute prefix
	vector<int> vec(dsize);
	vector<string> res;
	size_t pre=0;
	vec[0]=0;
	for(size_t i=1;i<dsize;++i){
		while(d[i]!=d[pre]&&pre!=0){
			pre=vec[pre-1];
		}
		if(d[i]==d[pre]){
			++pre;
		}
		vec[i]=pre;
	}
	//matching
	size_t last_index=0;
	pre=0;
	for(size_t i=0;i<s.size();++i){
		while(s[i]!=d[pre]&&pre!=0){
			pre=vec[pre-1];
		}
		if(s[i]==d[pre]){
			++pre;
		}
		if(pre==dsize){
			size_t length=i-last_index-dsize+1;
			pre=0;
			if(length!=0){
				res.emplace_back(s.substr(last_index,length));
			}
			last_index=i+1;
		}
	}
	size_t length=s.size()-last_index;
	if(length>0){
		res.emplace_back(s.substr(last_index,length));
	}
	return res;
}
