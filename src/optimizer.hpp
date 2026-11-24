#pragma once
// Standalone subset-LLVM-IR optimizer. SSA constant/copy propagation, local CSE, DCE.
// Side-effecting instructions (store/call/branch/ret) never removed.
#include <sstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdint>
#include <climits>
#include <algorithm>
namespace local_opt {
using std::string;
static string trim(string v){auto a=v.find_first_not_of(" \t\r");auto b=v.find_last_not_of(" \t\r");return a==string::npos?"":v.substr(a,b-a+1);}
static int32_t wrap(int64_t v){return static_cast<int32_t>(static_cast<uint32_t>(v));}
static bool fold(const string&rhs,int32_t&res){
 std::smatch m;
 if(std::regex_match(rhs,m,std::regex(R"((add|sub|mul|sdiv|srem) i32 (-?[0-9]+), (-?[0-9]+))"))){
  int64_t a=wrap(std::stoll(m[2])),b=wrap(std::stoll(m[3]));string op=m[1];
  if(op=="add")res=wrap(a+b);else if(op=="sub")res=wrap(a-b);else if(op=="mul")res=wrap(a*b);
  else if(b==0||(a==INT32_MIN&&b==-1))return false;
  else if(op=="sdiv")res=wrap(a/b);else res=wrap(a%b);
  return true;
 }
 if(std::regex_match(rhs,m,std::regex(R"(icmp (eq|ne|slt|sle|sgt|sge) i32 (-?[0-9]+), (-?[0-9]+))"))){
  int64_t a=wrap(std::stoll(m[2])),b=wrap(std::stoll(m[3]));string op=m[1];
  res=op=="eq"?a==b:op=="ne"?a!=b:op=="slt"?a<b:op=="sle"?a<=b:op=="sgt"?a>b:a>=b;return true;
 }
 if(std::regex_match(rhs,m,std::regex(R"(zext i1 (-?[0-9]+) to i32)"))){res=std::stoi(m[1])&1;return true;}
 return false;
}
static string sub(const string&line,const std::unordered_map<string,string>&repls){
 static const std::regex token(R"(%[A-Za-z_][A-Za-z0-9_.]*)");
 string ans;std::sregex_iterator i(line.begin(),line.end(),token),end;size_t pos=0;
 for(;i!=end;++i){ans+=line.substr(pos,static_cast<size_t>(i->position())-pos);auto it=repls.find(i->str());ans+=it==repls.end()?i->str():it->second;pos=i->position()+i->length();}
 ans+=line.substr(pos);return ans;
}
static string optimizeSegment(const string& ir){
 std::istringstream in(ir);std::vector<string> lines;string s;
 while(std::getline(in,s))lines.push_back(s);
 std::unordered_map<string,string> replaced;
 // Every iteration uses the current SSA constants; no memory value is rewritten.
 for(int round=0;round<7;++round){
  bool changed=false;
  for(auto &line:lines){
   size_t eq=line.find(" = ");
   if(eq==string::npos){string next=sub(line,replaced);if(next!=line){line=next;changed=true;}continue;}
   string left=trim(line.substr(0,eq)),right=trim(line.substr(eq+3));
   string newright=sub(right,replaced);
   if(newright!=right){right=newright;line="  "+left+" = "+right;changed=true;}
   int32_t val=0;
   if(fold(right,val)){
    if(!replaced.count(left)){replaced[left]=std::to_string(val);changed=true;}
    continue;
   }
   std::smatch m;
   if(std::regex_match(right,m,std::regex(R"(add i32 (.+), 0)"))||
      std::regex_match(right,m,std::regex(R"(sub i32 (.+), 0)"))||
      std::regex_match(right,m,std::regex(R"(mul i32 (.+), 1)"))){
     if(!replaced.count(left)){replaced[left]=m[1];changed=true;}
   }
  }
  if(!changed)break;
 }
 for(auto &line:lines){
  auto eq=line.find(" = ");
  if(eq==string::npos)line=sub(line,replaced);
  else line=line.substr(0,eq+3)+sub(line.substr(eq+3),replaced);
 }
 // Local CSE: reuse a pure instruction only inside the same basic block.
 std::unordered_map<string,string> available;
 for(auto &line:lines){
  string t=trim(line);
  if(!t.empty()&&t.back()==':'){available.clear();continue;}
  size_t eq=t.find(" = ");if(eq==string::npos)continue;
  string left=t.substr(0,eq),right=t.substr(eq+3);
  if(!std::regex_search(right,std::regex(R"(^(add|sub|mul|sdiv|srem|icmp|zext) )")))continue;
  auto it=available.find(right);
  if(it!=available.end())replaced[left]=it->second;
  else available[right]=left;
 }
 for(auto &line:lines){auto eq=line.find(" = ");if(eq==string::npos)line=sub(line,replaced);else line=line.substr(0,eq+3)+sub(line.substr(eq+3),replaced);}
 // Delete unused pure definitions; repeat to delete cascading dead computations.
 std::unordered_set<size_t> dead;
 for(int iteration=0;iteration<10;++iteration){
  std::unordered_map<string,int> uses;
  static const std::regex token(R"(%[A-Za-z_][A-Za-z0-9_.]*)");
  for(size_t j=0;j<lines.size();++j)if(!dead.count(j)){
   auto eq=lines[j].find(" = ");string rhs=eq==string::npos?lines[j]:lines[j].substr(eq+3);
   for(std::sregex_iterator i(rhs.begin(),rhs.end(),token),end;i!=end;++i)++uses[i->str()];
  }
  bool changed=false;
  for(size_t j=0;j<lines.size();++j)if(!dead.count(j)){
   string t=trim(lines[j]);auto eq=t.find(" = ");if(eq==string::npos)continue;
   string lhs=t.substr(0,eq),rhs=t.substr(eq+3);
   bool pure=std::regex_search(rhs,std::regex(R"(^(add|sub|mul|sdiv|srem|icmp|zext|phi|load) )"));
   if((pure||replaced.count(lhs))&&uses[lhs]==0){dead.insert(j);changed=true;}
  }
  if(!changed)break;
 }
 std::ostringstream out;
 for(size_t j=0;j<lines.size();++j)if(!dead.count(j))out<<lines[j]<<"\n";
 return out.str();
}
static string optimize(const string&ir){
 std::istringstream in(ir);std::ostringstream out;string line,segment;bool inside=false;
 while(std::getline(in,line)){
  if(line.rfind("define ",0)==0)inside=true;
  if(inside){segment+=line+"\n";if(line=="}"){out<<optimizeSegment(segment);segment.clear();inside=false;}}
  else out<<line<<"\n";
 }
 if(!segment.empty())out<<optimizeSegment(segment);
 return out.str();
}

}
