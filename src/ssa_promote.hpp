#pragma once
// Local-alloca promotion for the ToyC LLVM-IR subset, implemented without LLVM.
// This is a forward SSA dataflow construction, with edge-specific phi inputs.
// Conservatively promotes only entry-block initialized scalar alloca values
// whose addresses do not escape and are only accessed through direct load/store.
#include <algorithm>
#include <map>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>
namespace toyc_ssa {
using std::string;
using std::vector;
using std::unordered_map;
using std::unordered_set;
static string trim(string s){auto a=s.find_first_not_of(" \t\r"),b=s.find_last_not_of(" \t\r");return a==string::npos?"":s.substr(a,b-a+1);}
static bool isLabel(const string&s){return !s.empty()&&s.back()==':';}
struct Basic {string name;vector<string> lines;vector<int> preds,succs;bool reachable=false;};
static bool parseStore(const string&s,string&v,string&a){
 static const std::regex re(R"(^store i32 (\S+), ptr (%[A-Za-z0-9_.]+)(?:, align [0-9]+)?$)");
 std::smatch m;if(!std::regex_match(s,m,re))return false;v=m[1];a=m[2];return true;
}
static bool parseLoad(const string&s,string&v,string&a){
 static const std::regex re(R"(^(%[A-Za-z0-9_.]+) = load i32, ptr (%[A-Za-z0-9_.]+)(?:, align [0-9]+)?$)");
 std::smatch m;if(!std::regex_match(s,m,re))return false;v=m[1];a=m[2];return true;
}
static vector<string> targets(const string&line){
 static const std::regex re(R"(label %([A-Za-z0-9_.]+))");
 vector<string> out;
 for(std::sregex_iterator it(line.begin(),line.end(),re),end;it!=end;++it)out.push_back((*it)[1]);
 return out;
}
static string replaceRefs(const string&s,const unordered_map<string,string>&aliases){
 static const std::regex token(R"(%[A-Za-z_][A-Za-z0-9_.]*)");
 string out;size_t p=0;
 for(std::sregex_iterator it(s.begin(),s.end(),token),end;it!=end;++it){
  out+=s.substr(p,static_cast<size_t>(it->position())-p);
  string v=it->str();unordered_set<string>seen;
  while(aliases.count(v)&&!seen.count(v)){seen.insert(v);v=aliases.at(v);}
  out+=v;p=it->position()+it->length();
 }
 return out+s.substr(p);
}
static string promoteFunction(const string&segment){
 std::istringstream in(segment);string header,line;std::getline(in,header);
 vector<Basic> blocks;bool closed=false;
 while(std::getline(in,line)){
  string t=trim(line);if(t=="}"){closed=true;break;}
  if(isLabel(t))blocks.push_back({t.substr(0,t.size()-1),{},{},{},false});
  else if(!blocks.empty())blocks.back().lines.push_back(t);
 }
 if(!closed||blocks.empty())return segment;
 unordered_map<string,int> byName;
 for(size_t i=0;i<blocks.size();++i)byName[blocks[i].name]=static_cast<int>(i);
 for(size_t i=0;i<blocks.size();++i)if(!blocks[i].lines.empty()){
  for(auto&target:targets(blocks[i].lines.back())){
   if(byName.count(target)){
    int j=byName[target];blocks[i].succs.push_back(j);blocks[j].preds.push_back(static_cast<int>(i));
   }
  }
 }
 std::vector<int> stack{0};blocks[0].reachable=true;
 for(size_t k=0;k<stack.size();++k)for(int j:blocks[stack[k]].succs){
  if(!blocks[j].reachable){blocks[j].reachable=true;stack.push_back(j);}
 }
 // Collect only allocas with a first initialization in entry and direct loads/stores.
 static const std::regex allocaRe(R"(^(%[A-Za-z0-9_.]+) = alloca i32, align 4$)");
 unordered_set<string> allocas,initialized,escaped;
 for(auto&s:blocks[0].lines){std::smatch m;if(std::regex_match(s,m,allocaRe))allocas.insert(m[1]);}
 for(auto&s:blocks[0].lines){string val,addr;if(parseStore(s,val,addr)&&allocas.count(addr))initialized.insert(addr);}
 for(const auto&b:blocks)for(const auto&s:b.lines){
  static const std::regex token(R"(%[A-Za-z_][A-Za-z0-9_.]*)");
  string val,addr;
  if(parseStore(s,val,addr)){if(allocas.count(val))escaped.insert(val);continue;}
  if(parseLoad(s,val,addr))continue;
  std::smatch m;if(std::regex_match(s,m,allocaRe))continue;
  for(std::sregex_iterator it(s.begin(),s.end(),token),end;it!=end;++it)
   if(allocas.count(it->str()))escaped.insert(it->str());
 }
 vector<string> eligible;
 for(auto&v:allocas)if(initialized.count(v)&&!escaped.count(v))eligible.push_back(v);
 std::sort(eligible.begin(),eligible.end());
 if(eligible.empty())return segment;
 // Check that the entry-block first store dominates its first read.
 for(auto it=eligible.begin();it!=eligible.end();){
  bool seen=false,okay=true;
  for(auto&s:blocks[0].lines){string value,addr;
   if(parseLoad(s,value,addr)&&addr==*it&&!seen){okay=false;break;}
   if(parseStore(s,value,addr)&&addr==*it)seen=true;
  }
  if(!okay)it=eligible.erase(it);else ++it;
 }
 if(eligible.empty())return segment;
 // Generate an incoming phi at each reachable join (including loop headers).
 int maxTemp=0;static const std::regex reg(R"(%t([0-9]+))");
 for(const auto&b:blocks)for(const auto&s:b.lines)
  for(std::sregex_iterator it(s.begin(),s.end(),reg),end;it!=end;++it)maxTemp=std::max(maxTemp,std::stoi((*it)[1])+1);
 unordered_map<string,vector<string>> phi;
 for(const auto&v:eligible){
  phi[v].resize(blocks.size());
  for(size_t i=1;i<blocks.size();++i)if(blocks[i].reachable&&blocks[i].preds.size()>1)
   phi[v][i]="%t"+std::to_string(maxTemp++);
 }
 // The value reaching the block end can be its last store or its entry value.
 // All load use sites refer to the current SSA value, resolved lazily from CFG.
 unordered_map<string,string> aliases;
 std::map<std::pair<string,int>,string> endMemo;
 std::unordered_set<string> visiting;
 std::function<string(const string&,int)> atEnd;
 std::function<string(const string&,int)> atStart;
 atStart=[&](const string&v,int i)->string{
  if(!phi[v][i].empty())return phi[v][i];
  if(blocks[i].preds.empty())return "undef";
  return atEnd(v,blocks[i].preds.front());
 };
 atEnd=[&](const string&v,int i)->string{
  auto key=std::make_pair(v,i);if(endMemo.count(key))return endMemo[key];
  string visit=v+"#"+std::to_string(i);
  if(visiting.count(visit))return "undef"; // unreachable-only cycle
  visiting.insert(visit);
  string last;
  for(auto&s:blocks[i].lines){string val,addr;if(parseStore(s,val,addr)&&addr==v)last=val;}
  if(last.empty())last=atStart(v,i);
  visiting.erase(visit);endMemo[key]=last;return last;
 };
 // Alias all promoted loads to the value of the currently reaching store.
 for(size_t i=0;i<blocks.size();++i){
  unordered_map<string,string> current;
  for(auto&v:eligible)current[v]=atStart(v,static_cast<int>(i));
  for(auto&s:blocks[i].lines){
   string value,addr;
   if(parseStore(s,value,addr)&&current.count(addr)){current[addr]=value;continue;}
   if(parseLoad(s,value,addr)&&current.count(addr))aliases[value]=current[addr];
  }
 }
 // Fill phi operands from CFG edges. Drop unused phi later via ordinary DCE.
 vector<vector<string>> heads(blocks.size());
 for(auto&v:eligible){
  for(size_t i=1;i<blocks.size();++i)if(!phi[v][i].empty()){
   string line="  "+phi[v][i]+" = phi i32 ";bool first=true;
   for(int p:blocks[i].preds){
    if(!blocks[p].reachable)continue;
    if(!first)line+=", ";
    first=false;
    line+="[ "+atEnd(v,p)+", %"+blocks[p].name+" ]";
   }
   heads[i].push_back(line);
  }
 }
 // Collapse phi that merely transports an unchanged input around a loop,
 // e.g. phi [v, entry], [self, backedge]. Repeat until stable.
 for(int iteration=0;iteration<12;++iteration){
  bool changed=false;
  for(size_t i=0;i<heads.size();++i)for(const auto&s:heads[i]){
   auto eq=s.find(" = ");if(eq==string::npos)continue;
   string def=trim(s.substr(0,eq));
   if(aliases.count(def))continue;
   static const std::regex source(R"(\[\s*([^,]+),\s*%[A-Za-z0-9_.]+\s*\])");
   string value;bool good=true,atleast=false;
   for(std::sregex_iterator it(s.begin(),s.end(),source),end;it!=end;++it){
    string v=replaceRefs(trim((*it)[1]),aliases);
    if(v==def)continue;
    if(!atleast){value=v;atleast=true;}
    else if(v!=value){good=false;break;}
   }
   if(good&&atleast&&value!="undef"){aliases[def]=value;changed=true;}
  }
  if(!changed)break;
 }
 std::ostringstream out;out<<header<<"\n";
 for(size_t i=0;i<blocks.size();++i){
  out<<blocks[i].name<<":\n";
  for(auto&s:heads[i]){
   string def=trim(s.substr(0,s.find(" = ")));
   if(!aliases.count(def))out<<replaceRefs(s.substr(0,s.find(" = ")+3),{})<<replaceRefs(s.substr(s.find(" = ")+3),aliases)<<"\n";
  }
  for(const auto&s:blocks[i].lines){
   string value,addr;std::smatch m;
   if(std::regex_match(s,m,allocaRe)&&std::find(eligible.begin(),eligible.end(),m[1].str())!=eligible.end())continue;
   if(parseStore(s,value,addr)&&std::find(eligible.begin(),eligible.end(),addr)!=eligible.end())continue;
   if(parseLoad(s,value,addr)&&std::find(eligible.begin(),eligible.end(),addr)!=eligible.end())continue;
   size_t eq=s.find(" = ");
   if(eq!=string::npos)out<<"  "<<s.substr(0,eq+3)<<replaceRefs(s.substr(eq+3),aliases)<<"\n";
   else out<<"  "<<replaceRefs(s,aliases)<<"\n";
  }
 }
 out<<"}\n";return out.str();
}
static string promote(const string&ir){
 std::istringstream in(ir);std::ostringstream out;string line,seg;bool inFun=false;
 while(std::getline(in,line)){
  if(line.rfind("define ",0)==0)inFun=true;
  if(inFun){seg+=line+"\n";if(trim(line)=="}"){out<<promoteFunction(seg);seg.clear();inFun=false;}}
  else out<<line<<"\n";
 }
 if(!seg.empty())out<<seg;
 return out.str();
}
}
