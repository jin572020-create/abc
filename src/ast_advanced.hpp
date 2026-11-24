#pragma once
// Conservative, manually implemented AST interprocedural and loop passes.
// All inlining candidates are straight-line, side-effect-free and total.
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
namespace toyc_advanced {
using std::string;
using std::vector;
using std::unordered_map;
using std::unordered_set;
static Node clone(const Node&x){
 if(!x)return {};
 auto n=std::make_shared<AST>(*x);n->children.clear();
 for(auto&c:x->children)n->children.push_back(clone(c));
 return n;
}
static void collectNames(const Node&x,unordered_set<string>&names){
 if(!x)return;
 if(x->type=="Decl"||x->type=="ConstDecl"||x->type=="Param"||x->type=="Variable"||x->type=="Function")names.insert(x->text);
 for(auto&c:x->children)collectNames(c,names);
}
static int nodes(const Node&n){if(!n)return 0;int k=1;for(auto&x:n->children)k+=nodes(x);return k;}
static void writes(const Node&n,unordered_set<string>&names){
 if(!n)return;
 if(n->type=="Assign"||n->type=="Decl"||n->type=="ConstDecl")names.insert(n->text);
 for(auto&c:n->children)writes(c,names);
}
static bool containsAny(const Node&n,const unordered_set<string>&names){
 if(!n)return false;
 if(n->type=="Variable"&&names.count(n->text))return true;
 for(auto&c:n->children)if(containsAny(c,names))return true;
 return false;
}
static bool hasCall(const Node&n){
 if(!n)return false;
 if(n->type=="Call")return true;
 for(auto&c:n->children)if(hasCall(c))return true;
 return false;
}
static bool safeTotalExpr(const Node&n){
 if(!n)return true;
 if(n->type=="Call")return false;
 if(n->type=="Binary"&&(n->text=="/"||n->text=="%")){
  if(n->children.size()!=2||n->children[1]->type!="Number")return false;
  try{if(std::stoll(n->children[1]->text)==0)return false;}catch(...){return false;}
 }
 for(auto&c:n->children)if(!safeTotalExpr(c))return false;
 return true;
}
struct Candidate{vector<string>params;Node expression;};
static Node substitute(const Node&n,const unordered_map<string,Node>&values){
 if(!n)return {};
 if(n->type=="Variable"){
  auto it=values.find(n->text);if(it!=values.end())return clone(it->second);
 }
 auto r=std::make_shared<AST>(*n);r->children.clear();
 for(auto&c:n->children)r->children.push_back(substitute(c,values));
 return r;
}
static bool candidateFor(const Node&f,Candidate&c){
 if(!f||f->type!="Function"||f->children.empty()||f->children[0]->text=="void")return false;
 const auto&body=f->children.back();if(!body||body->type!="Block")return false;
 if(body->children.empty()||body->children.size()>14)return false;
 c.params.clear();
 unordered_map<string,Node> defs;unordered_set<string>names;
 for(size_t i=1;i+1<f->children.size();++i){string p=f->children[i]->text;c.params.push_back(p);names.insert(p);}
 for(size_t i=0;i<body->children.size();++i){
  const auto&stmt=body->children[i];
  if((stmt->type=="Decl"||stmt->type=="ConstDecl")&&stmt->children.size()==1&&i+1<body->children.size()){
   if(names.count(stmt->text)||!safeTotalExpr(stmt->children[0]))return false;
   auto val=substitute(stmt->children[0],defs);
   if(!safeTotalExpr(val)||nodes(val)>55)return false;
   names.insert(stmt->text);defs[stmt->text]=val;
  }else if(stmt->type=="Return"&&stmt->children.size()==1&&i+1==body->children.size()){
   if(!safeTotalExpr(stmt->children[0]))return false;
   c.expression=substitute(stmt->children[0],defs);
   return safeTotalExpr(c.expression)&&nodes(c.expression)<=2500;
  }else return false;
 }
 return false;
}
static string unique(unordered_set<string>&used,int&counter){
 string s;do{s="__toyc_opt_"+std::to_string(counter++);}while(used.count(s));
 used.insert(s);return s;
}
static Node makeNode(const string&type,const string&text,int line,vector<Node> children={}){
 auto n=std::make_shared<AST>();n->type=type;n->text=text;n->line=line;n->children=std::move(children);return n;
}
static void hoist(Node&n,const unordered_set<string>&pureFunctions,unordered_set<string>&used,int&counter){
 if(!n)return;
 for(auto&c:n->children)hoist(c,pureFunctions,used,counter);
 if(n->type!="Block")return;
 for(size_t i=0;i<n->children.size();++i){
  Node w=n->children[i];
  if(!w||w->type!="While"||w->children.size()!=2||w->children[1]->type!="Block")continue;
  auto&body=w->children[1];unordered_set<string>mutated;writes(body,mutated);
  vector<Node> inserted;
  for(auto&stmt:body->children){
   if(!stmt||(stmt->type!="Decl"&&stmt->type!="ConstDecl")||stmt->children.size()!=1)continue;
   Node expr=stmt->children[0];
   if(!expr||expr->type!="Call"||!pureFunctions.count(expr->text))continue;
   bool okay=true;
   for(auto&arg:expr->children)if(!safeTotalExpr(arg)||containsAny(arg,mutated))okay=false;
   if(!okay)continue;
   string name=unique(used,counter);
   inserted.push_back(makeNode("Decl",name,stmt->line,{expr}));
   stmt->children[0]=makeNode("Variable",name,stmt->line);
  }
  if(!inserted.empty()){
   n->children.insert(n->children.begin()+static_cast<long>(i),inserted.begin(),inserted.end());
   i+=inserted.size();
  }
 }
}
static void inlineExpr(Node&n,const unordered_map<string,Candidate>&cands,int depth=0){
 if(!n||depth>8)return;
 for(auto&ch:n->children)inlineExpr(ch,cands,depth+1);
 if(n->type!="Call")return;
 auto it=cands.find(n->text);
 if(it==cands.end()||it->second.params.size()!=n->children.size())return;
 // Repeating an argument is only safe when it cannot call, trap or mutate state.
 for(auto&arg:n->children)if(!safeTotalExpr(arg))return;
 unordered_map<string,Node> actual;
 for(size_t i=0;i<n->children.size();++i)actual[it->second.params[i]]=n->children[i];
 auto expanded=substitute(it->second.expression,actual);
 if(nodes(expanded)>5000)return;
 n=expanded;
}
static void inlineCalls(Node&root){
 // Iterating lets blend become inlineable after adjust is expanded.
 for(int round=0;round<3;++round){
  unordered_map<string,Candidate>cands;
  for(auto&f:root->children){Candidate c;if(candidateFor(f,c))cands[f->text]=c;}
  if(cands.empty())return;
  for(auto&f:root->children){
   if(cands.count(f->text))continue; // Never replace body of a candidate with itself.
   for(auto&c:f->children)inlineExpr(c,cands);
  }
 }
}
static bool tailRecursive(const Node&f){
 if(!f||f->type!="Function"||f->children.size()<3||f->children[0]->text=="void")return false;
 auto body=f->children.back();if(!body||body->type!="Block"||body->children.size()<2)return false;
 auto last=body->children.back();
 if(last->type!="Return"||last->children.size()!=1)return false;
 auto call=last->children[0];
 if(call->type!="Call"||call->text!=f->text||call->children.size()!=f->children.size()-2)return false;
 // Retain only original shapes with a guaranteed base return and a single recursive call.
 if(body->children.front()->type!="If")return false;
 for(size_t i=0;i+1<body->children.size();++i)if(hasCall(body->children[i]))return false;
 return true;
}
static void tailEliminate(Node&root,unordered_set<string>&used,int&counter){
 for(auto&f:root->children){
  if(!tailRecursive(f))continue;
  auto body=f->children.back();
  auto original=body->children;
  auto call=original.back()->children[0];original.pop_back();
  // Evaluate every argument into an independent temporary before any assignment.
  vector<Node> temps;
  for(size_t i=0;i<call->children.size();++i){
   string tmp=unique(used,counter);
   original.push_back(makeNode("Decl",tmp,call->line,{call->children[i]}));
   temps.push_back(makeNode("Variable",tmp,call->line));
  }
  for(size_t i=0;i<temps.size();++i){
   original.push_back(makeNode("Assign",f->children[i+1]->text,call->line,{temps[i]}));
  }
  Node inner=makeNode("Block","",body->line,original);
  Node one=makeNode("Number","1",body->line);
  Node loop=makeNode("While","while",body->line,{one,inner});
  // The fallback is unreachable but satisfies the unoptimized emitter's
  // requirement that an i32 function has an explicit terminal return.
  Node fallback=makeNode("Return","return",body->line,{makeNode("Number","0",body->line)});
  body->children={loop,fallback};
 }
}
static bool incByOne(const Node&n,const string&name){
 if(!n||n->type!="Assign"||n->text!=name||n->children.size()!=1)return false;
 auto e=n->children[0];
 return e->type=="Binary"&&e->text=="+"&&e->children.size()==2&&
        e->children[0]->type=="Variable"&&e->children[0]->text==name&&
        e->children[1]->type=="Number"&&e->children[1]->text=="1";
}
static void mulConstants(const Node&n,const string&var,unordered_set<string>&values){
 if(!n)return;
 if(n->type=="Binary"&&n->text=="*"&&n->children.size()==2&&
    n->children[0]->type=="Variable"&&n->children[0]->text==var&&
    n->children[1]->type=="Number")values.insert(n->children[1]->text);
 for(auto&c:n->children)mulConstants(c,var,values);
}
static void replaceMultiplications(Node&n,const string&var,const unordered_map<string,string>&names){
 if(!n)return;
 if(n->type=="Binary"&&n->text=="*"&&n->children.size()==2&&
    n->children[0]->type=="Variable"&&n->children[0]->text==var&&
    n->children[1]->type=="Number"){
  auto it=names.find(n->children[1]->text);
  if(it!=names.end()){
   n=makeNode("Variable",it->second,n->line);return;
  }
 }
 for(auto&c:n->children)replaceMultiplications(c,var,names);
}
static void inductionStrength(Node&n,unordered_set<string>&used,int&counter){
 if(!n)return;
 for(auto&c:n->children)inductionStrength(c,used,counter);
 if(n->type!="Block")return;
 for(size_t k=1;k<n->children.size();++k){
  auto prior=n->children[k-1],loop=n->children[k];
  if(!prior||prior->type!="Decl"||prior->children.size()!=1||prior->children[0]->type!="Number")continue;
  if(!loop||loop->type!="While"||loop->children.size()!=2||loop->children[1]->type!="Block")continue;
  auto cond=loop->children[0];
  if(!cond||cond->type!="Binary"||(cond->text!="<"&&cond->text!="<="))continue;
  if(cond->children[0]->type!="Variable"||cond->children[0]->text!=prior->text)continue;
  auto body=loop->children[1];if(body->children.empty()||!incByOne(body->children.back(),prior->text))continue;
  int assignments=0;bool unsafe=false;
  for(size_t i=0;i<body->children.size();++i){
   auto st=body->children[i];
   if(st->type=="Break"||st->type=="Continue"||st->type=="Return"||st->type=="If"||st->type=="While"||st->type=="Block")unsafe=true;
   if(st->type=="Assign"&&st->text==prior->text)++assignments;
  }
  if(unsafe||assignments!=1)continue;
  unordered_set<string> ks;mulConstants(body,prior->text,ks);
  if(ks.empty()||ks.size()>8)continue;
  unordered_map<string,string> subs;
  vector<Node> decls;
  for(const auto&str:ks){
   int64_t val=0;try{val=std::stoll(str);}catch(...){continue;}
   if(val<=0||val>1000000)continue;
   string name=unique(used,counter);subs[str]=name;
   auto mul=makeNode("Binary","*",loop->line,{
      makeNode("Variable",prior->text,loop->line),makeNode("Number",str,loop->line)});
   decls.push_back(makeNode("Decl",name,loop->line,{mul}));
  }
  if(decls.empty())continue;
  for(size_t i=0;i+1<body->children.size();++i)
   replaceMultiplications(body->children[i],prior->text,subs);
  for(const auto&name:subs){
   auto add=makeNode("Binary","+",loop->line,{
      makeNode("Variable",name.second,loop->line),makeNode("Number",name.first,loop->line)});
   body->children.push_back(makeNode("Assign",name.second,loop->line,{add}));
  }
  n->children.insert(n->children.begin()+static_cast<long>(k),decls.begin(),decls.end());
  k+=decls.size();
 }
}
static bool sameExpr(const Node&a,const Node&b){
 if(!a||!b)return !a&&!b;
 if(a->type!=b->type||a->text!=b->text||a->children.size()!=b->children.size())return false;
 for(size_t i=0;i<a->children.size();++i)
  if(!sameExpr(a->children[i],b->children[i]))return false;
 return true;
}
static void matchingIfs(const Node&n,const Node&cond,int&count){
 if(!n)return;
 if(n->type=="If"&&!n->children.empty()&&sameExpr(n->children[0],cond))++count;
 for(auto&c:n->children)matchingIfs(c,cond,count);
}
static void branchResolve(Node&n,const Node&cond,bool truth){
 if(!n)return;
 if(n->type=="If"&&!n->children.empty()&&sameExpr(n->children[0],cond)){
  Node repl=truth?n->children[1]:(n->children.size()>2?n->children[2]:makeNode("Empty","",n->line));
  n=repl;branchResolve(n,cond,truth);return;
 }
 for(auto&c:n->children)branchResolve(c,cond,truth);
}
static void unswitch(Node&n){
 if(!n)return;
 for(auto&c:n->children)unswitch(c);
 if(n->type!="While"||n->children.size()!=2||n->children[1]->type!="Block")return;
 Node body=n->children[1];unordered_set<string>changed;writes(body,changed);
 Node invariant;
 for(auto&stmt:body->children){
  if(stmt->type=="If"&&stmt->children.size()>1&&safeTotalExpr(stmt->children[0])&&
     !containsAny(stmt->children[0],changed)){
   int count=0;matchingIfs(body,stmt->children[0],count);
   if(count>=2){invariant=stmt->children[0];break;}
  }
 }
 if(!invariant)return;
 Node yes=clone(n),no=clone(n);
 branchResolve(yes->children[1],invariant,true);
 branchResolve(no->children[1],invariant,false);
 n=makeNode("If","if",n->line,{clone(invariant),yes,no});
}
static void optimize(Node&root){
 unordered_set<string>used;collectNames(root,used);int counter=0;
 // Hoist proven-total, side-effect-free call expressions out of inner loops.
 unordered_set<string>pureFunctions;
 for(auto&f:root->children){Candidate c;if(candidateFor(f,c))pureFunctions.insert(f->text);}
 for(auto&f:root->children)hoist(f,pureFunctions,used,counter);
 inlineCalls(root);
 for(auto&f:root->children)unswitch(f);
 for(auto&f:root->children)inductionStrength(f,used,counter);
 tailEliminate(root,used,counter);
}
}
