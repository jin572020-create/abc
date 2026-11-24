#pragma once
// Dominance-safe cross-basic-block CSE for SSA arithmetic expressions.
// No value numbering of memory operations: only immutable SSA operands.
#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
namespace toyc_global_cse {
using std::string;
using std::vector;
static string trim(const string&x){auto a=x.find_first_not_of(" \t\r"),b=x.find_last_not_of(" \t\r");return a==string::npos?"":x.substr(a,b-a+1);}
struct B {string label;vector<int> lines,preds,succs;std::set<int>dom;bool reachable=false;};
static bool pure(const string&rhs){
 static const std::regex regex(R"(^(add|sub|mul|sdiv|srem|icmp|zext) )");
 return std::regex_search(rhs,regex);
}
static void apply(vector<string>&lines){
 vector<B> blocks;std::unordered_map<string,int>id;
 int current=-1;
 for(size_t i=0;i<lines.size();++i){
  string t=trim(lines[i]);
  if(!t.empty()&&t.back()==':'&&t.find(' ')==string::npos){
   current=static_cast<int>(blocks.size());
   blocks.push_back({t.substr(0,t.size()-1),{},{},{},{},false});
   id[blocks.back().label]=current;
  }else if(current>=0&&t!="}"&&!t.empty())blocks[current].lines.push_back(static_cast<int>(i));
 }
 if(blocks.empty())return;
 static const std::regex target(R"(label %([A-Za-z0-9_.]+))");
 for(size_t k=0;k<blocks.size();++k){
  if(blocks[k].lines.empty())continue;
  auto t=trim(lines[blocks[k].lines.back()]);
  for(std::sregex_iterator it(t.begin(),t.end(),target),end;it!=end;++it){
   auto j=id.find((*it)[1]);if(j==id.end())continue;
   blocks[k].succs.push_back(j->second);
   blocks[j->second].preds.push_back(static_cast<int>(k));
  }
 }
 vector<int>queue{0};blocks[0].reachable=true;
 for(size_t i=0;i<queue.size();++i)
  for(int s:blocks[queue[i]].succs)if(!blocks[s].reachable){blocks[s].reachable=true;queue.push_back(s);}
 std::set<int>all(queue.begin(),queue.end());
 for(size_t i=0;i<blocks.size();++i)blocks[i].dom=i==0?std::set<int>{0}:all;
 for(int iteration=0;iteration<100;++iteration){
  bool changed=false;
  for(size_t i=1;i<blocks.size();++i)if(blocks[i].reachable){
   bool first=true;std::set<int>d;
   for(int p:blocks[i].preds)if(blocks[p].reachable){
    if(first){d=blocks[p].dom;first=false;}
    else {std::set<int>inter;std::set_intersection(d.begin(),d.end(),blocks[p].dom.begin(),blocks[p].dom.end(),std::inserter(inter,inter.begin()));d.swap(inter);}
   }
   d.insert(static_cast<int>(i));
   if(d!=blocks[i].dom){blocks[i].dom.swap(d);changed=true;}
  }
  if(!changed)break;
 }
 struct Def{int block,pos;string lhs,rhs;};vector<Def>defs;
 for(size_t k=0;k<blocks.size();++k)if(blocks[k].reachable)
  for(size_t pos=0;pos<blocks[k].lines.size();++pos){
   int j=blocks[k].lines[pos];string t=trim(lines[j]);auto eq=t.find(" = ");
   if(eq==string::npos)continue;
   string rhs=t.substr(eq+3);
   if(pure(rhs))defs.push_back({static_cast<int>(k),static_cast<int>(pos),t.substr(0,eq),rhs});
  }
 std::unordered_map<string,string>aliases;
 for(const auto&d:defs){
  // Select a result that dominates this definition (and appears earlier if in same block).
  const Def*best=nullptr;
  for(const auto&candidate:defs){
   if(candidate.lhs==d.lhs||candidate.rhs!=d.rhs)continue;
   if(!blocks[d.block].dom.count(candidate.block))continue;
   if(candidate.block==d.block&&candidate.pos>=d.pos)continue;
   if(!best||blocks[candidate.block].dom.size()>blocks[best->block].dom.size()||
      (candidate.block==best->block&&candidate.pos>best->pos))best=&candidate;
  }
  if(best)aliases[d.lhs]=best->lhs;
 }
 if(aliases.empty())return;
 static const std::regex ssa(R"(%[A-Za-z_][A-Za-z0-9_.]*)");
 auto replace=[&](const string&x){
  string out;size_t p=0;
  for(std::sregex_iterator it(x.begin(),x.end(),ssa),end;it!=end;++it){
   out+=x.substr(p,static_cast<size_t>(it->position())-p);
   string v=it->str();std::set<string>seen;
   while(aliases.count(v)&&!seen.count(v)){seen.insert(v);v=aliases[v];}
   out+=v;p=it->position()+it->length();
  }
  return out+x.substr(p);
 };
 for(auto&line:lines){
  auto eq=line.find(" = ");
  if(eq==string::npos)line=replace(line);
  else line=line.substr(0,eq+3)+replace(line.substr(eq+3));
 }
}
}
