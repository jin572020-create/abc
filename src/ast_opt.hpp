#pragma once
// Conservative source-tree optimizations, independent of LLVM toolchain.
// Only delete proven finite loops whose updates cannot escape the loop.
#include <unordered_map>
#include <unordered_set>
#include <limits>
namespace toyc_ast_opt {
static bool number(const Node&n,int64_t&v){
 if(!n||n->type!="Number")return false;
 try{v=std::stoll(n->text);return true;}catch(...){return false;}
}
static bool pure(const Node&n){
 if(!n)return true;
 if(n->type=="Call")return false;
 if(n->type=="Binary"&&(n->text=="/"||n->text=="%")){
  int64_t denom=0;
  if(!number(n->children.at(1),denom)||denom==0)return false;
 }
 for(const auto&c:n->children)if(!pure(c))return false;
 return true;
}
static Node empty(const Node&n){auto p=std::make_shared<AST>();p->type="Empty";p->line=n?n->line:0;return p;}
static void vars(const Node&n,std::unordered_map<std::string,int>&counts){
 if(!n)return;
 if(n->type=="Variable")++counts[n->text];
 if(n->type=="Assign")++counts[n->text];
 for(auto&c:n->children)vars(c,counts);
}
static bool simpleFiniteLoop(const Node&n,const std::string&counter,int64_t initial){
 if(!n||n->type!="While"||n->children.size()!=2)return false;
 const auto&cond=n->children[0];
 if(!cond||cond->type!="Binary"||(cond->text!="<"&&cond->text!="<="))return false;
 if(cond->children.size()!=2||cond->children[0]->type!="Variable"||cond->children[0]->text!=counter)return false;
 int64_t bound=0;if(!number(cond->children[1],bound))return false;
 if(initial<0||initial>1000000||bound<0||bound>1000000||bound>=INT32_MAX-1)return false;
 // Body must contain exactly one counter += positive step; no other side effects
 Node body=n->children[1];if(!body)return false;
 std::vector<Node> items=body->type=="Block"?body->children:std::vector<Node>{body};
 bool update=false;
 for(auto&x:items){
  if(x->type=="Empty")continue;
  if((x->type=="Decl"||x->type=="ConstDecl")&&x->text!=counter){
   if(!x->children.empty()&&!pure(x->children[0]))return false;
   continue;
  }
  if(x->type!="Assign"||x->text!=counter||update||x->children.size()!=1)return false;
  const auto&e=x->children[0];
  if(!e||e->type!="Binary"||e->text!="+"||e->children.size()!=2||e->children[0]->type!="Variable"||e->children[0]->text!=counter)return false;
  int64_t step=0;if(!number(e->children[1],step)||step<=0||step>1000000)return false;
  update=true;
 }
 return update;
}
static void optimize(Node&n){
 if(!n)return;
 for(auto&c:n->children)optimize(c);
 if(n->type=="If"&&n->children.size()>=2){
  int64_t v=0;
  if(number(n->children[0],v)){
   n=(v!=0)?n->children[1]:(n->children.size()>2?n->children[2]:empty(n));
   optimize(n);
  }
 }
 if(n->type!="Block")return;
 // Local bounded loops whose only mutated state is their own private induction variable.
 for(size_t i=0;i<n->children.size();++i){
  Node cur=n->children[i];
  if(!cur||cur->type!="While")continue;
  // Search immediate previous declaration, not a more remote assignment.
  if(i==0)continue;
  const Node&decl=n->children[i-1];
  if(!decl||decl->type!="Decl"||decl->children.size()!=1)continue;
  int64_t init=0;if(!number(decl->children[0],init))continue;
  if(simpleFiniteLoop(cur,decl->text,init)) n->children[i]=empty(cur);
 }
 // Remove unused local declarations only when their initializers have no effects.
 // Recompute until chains of dead temporaries disappear; count names conservatively.
 for(int round=0;round<20;++round){
  std::unordered_map<std::string,int> counts;
  for(auto&c:n->children)vars(c,counts);
  bool changed=false;
  for(auto &c:n->children){
   if(c&&(c->type=="Decl"||c->type=="ConstDecl")&&counts[c->text]==0&&
      (c->children.empty()||pure(c->children[0]))){
    c=empty(c);changed=true;
   }
  }
  if(!changed)break;
 }
}
}
