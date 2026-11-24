#pragma once
// Standalone LLVM-IR-subset to RISC-V RV64GC emitter. No Clang/LLVM dependencies.
// Stack-slot lowering: each SSA value and ToyC alloca gets a frame slot; t0..t4 are scratch.
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <climits>

namespace native_toyc {
using std::string;
using std::vector;
using std::unordered_map;
struct Inst {string raw, lhs, opcode;};
struct Block {string name;vector<Inst> ins;};
struct Func {string name,ret;vector<string> params;vector<Block> blocks;};
static string trim(string s){size_t a=s.find_first_not_of(" \t\r"),b=s.find_last_not_of(" \t\r");return a==string::npos?"":s.substr(a,b-a+1);}
static bool starts(const string&s,const string&prefix){return s.compare(0,prefix.size(),prefix)==0;}
static string match1(const string&s,const string&pat){std::smatch m;if(std::regex_search(s,m,std::regex(pat)))return m[1].str();return "";}
static vector<string> splitArgs(const string& s){
 vector<string> a;size_t depth=0,start=0;
 for(size_t i=0;i<s.size();++i){if(s[i]=='['||s[i]=='(')++depth;else if(s[i]==']'||s[i]==')')--depth;else if(s[i]==','&&depth==0){a.push_back(trim(s.substr(start,i-start)));start=i+1;}}
 if(start<s.size())a.push_back(trim(s.substr(start)));
 return a;
}
static vector<Func> parse(const string&ir){
 vector<Func> out;std::istringstream in(ir);string line;Func* f=nullptr;
 std::regex def(R"(^define\s+(i32|void)\s+@([A-Za-z_][\w.]*)\((.*)\)\s*\{$)");
 while(std::getline(in,line)){
  line=trim(line);if(line.empty()||line[0]==';'||starts(line,"target ")||starts(line,"source_")||starts(line,"declare "))continue;
  std::smatch m;
  if(std::regex_match(line,m,def)){
   out.push_back({m[2].str(),m[1].str(),{},{}});f=&out.back();
   for(auto &a:splitArgs(m[3].str())){string p=match1(a,R"((%[\w.]+)$)");if(!p.empty())f->params.push_back(p);}continue;
  }
  if(line=="}"){f=nullptr;continue;}
  if(!f)throw std::runtime_error("unknown module line: "+line);
  if(line.back()==':'){f->blocks.push_back({line.substr(0,line.size()-1),{}});continue;}
  if(f->blocks.empty())throw std::runtime_error("instruction before block: "+line);
  Inst z;z.raw=line;
  size_t eq=line.find(" = ");string rhs=line;
  if(eq!=string::npos){z.lhs=line.substr(0,eq);rhs=line.substr(eq+3);}
  size_t sp=rhs.find(' ');z.opcode=sp==string::npos?rhs:rhs.substr(0,sp);
  f->blocks.back().ins.push_back(z);
 }
 return out;
}
static string sym(const string&s){string r;for(char c:s)r+=std::isalnum((unsigned char)c)||c=='_'?c:'_';return r;}
static int round16(int n){return (n+15)&~15;}
static bool num(const string&v){return !v.empty()&&(std::isdigit((unsigned char)v[0])||v[0]=='-');}
static int32_t i32(int64_t x){return static_cast<int32_t>(static_cast<uint32_t>(x));}
struct Generator{
 std::ostringstream out;
 bool opt=false;
 string fname;
 unordered_map<string,int> slots;
 unordered_map<string,int64_t> known;
 unordered_map<string,string> promoted;
 unordered_set<string> promotedSSA;
 string lastSSA;
 vector<string> calleeRegs;
 int savedRegsBase=0;
 unordered_map<string,vector<std::pair<string,string>>> phi;
 int frame=0,used=0,outgoing=0,savedRA=0,savedS0=0;
 void emit(const string&s){out<<"    "<<s<<"\n";}
 void label(const string&s){out<<s<<":\n";}
 string bb(const string&s){return ".L"+sym(fname)+"_"+sym(s);}
 string epilogue(){return ".L"+sym(fname)+"_exit";}
 void address(int off,const string&r="t5"){
  if(off>=-2048&&off<=2047){return;}
  emit("li "+r+", "+std::to_string(off));emit("add "+r+", sp, "+r);
 }
 void loadRaw(const string&r,int off,bool wide=false){
  string op=wide?"ld ":"lw ";
  if(off>=-2048&&off<=2047)emit(op+r+", "+std::to_string(off)+"(sp)");
  else {address(off);emit(op+r+", 0(t5)");}
 }
 void storeRaw(const string&r,int off,bool wide=false){
  string op=wide?"sd ":"sw ";
  if(off>=-2048&&off<=2047)emit(op+r+", "+std::to_string(off)+"(sp)");
  else {address(off);emit(op+r+", 0(t5)");}
 }
 int slot(const string&s)const {auto i=slots.find(s);if(i==slots.end())throw std::runtime_error("undefined SSA value: "+s+" in "+fname);return i->second;}
 void put(const string&v,const string&r){
  if(v.empty())return;
  if(opt && promotedSSA.count(v))emit("mv "+promoted.at(v)+", "+r);
  else storeRaw(r,slot(v));
  if(opt && r!="t4") {emit("mv t4, "+r);lastSSA=v;}
  else lastSSA.clear();
 }
 void get(string v,const string&r){
  v=trim(v);if(v.empty())throw std::runtime_error("missing operand");
  if(num(v))emit("li "+r+", "+std::to_string(i32(std::stoll(v))));
  else if(opt&&known.count(v))emit("li "+r+", "+std::to_string(i32(known.at(v))));
  else if(opt && promotedSSA.count(v))emit("mv "+r+", "+promoted.at(v));
  else if(opt && v==lastSSA){if(r!="t4")emit("mv "+r+", t4");}
  else loadRaw(r,slot(v));
 }
 string operand(const string&s){auto p=s.find_last_of(' ');return trim(p==string::npos?s:s.substr(p+1));}
 void branches(const string&from,const string&dest){
  auto x=phi.find(dest);if(x==phi.end())return;
  // Stage source values before writing any phi results: parallel-copy semantics.
  vector<std::pair<string,string>> pairs;
  for(auto &i:x->second){auto in=match1(i.second,"\\[\\s*([^,]+),\\s*%"+from+"\\s*\\]");if(in.empty())throw std::runtime_error("missing phi incoming on "+from+" -> "+dest);pairs.push_back({i.first,trim(in)});}
  // phi result temporaries are allocated separately, and should not overwrite any inputs during staging.
  for(size_t k=0;k<pairs.size();++k){get(pairs[k].second,"t0");storeRaw("t0",used+static_cast<int>(k)*8);}
  for(size_t k=0;k<pairs.size();++k){loadRaw("t0",used+static_cast<int>(k)*8);put(pairs[k].first,"t0");}
 }
 void go(const string&from,const string&dest){branches(from,dest);emit("j "+bb(dest));}
 void directBranch(const string&b,int index,const string&pred,
                   const string&a,const string&c,const string&t,const string&f){
  get(a,"t0");get(c,"t1");
  string mn,left="t0",right="t1";
  if(pred=="eq")mn="beq";
  else if(pred=="ne")mn="bne";
  else if(pred=="slt")mn="blt";
  else if(pred=="sge")mn="bge";
  else if(pred=="sgt"){mn="blt";left="t1";right="t0";}
  else if(pred=="sle"){mn="bge";left="t1";right="t0";}
  else throw std::runtime_error("unknown direct branch predicate "+pred);
  bool trueHasPhi=phi.count(t)&&!phi.at(t).empty();
  string trampoline=bb(b)+"_cmp_edge_"+std::to_string(index);
  emit(mn+" "+left+", "+right+", "+(trueHasPhi?trampoline:bb(t)));
  go(b,f);
  if(trueHasPhi){label(trampoline);go(b,t);}
 }
 void inst(const Inst&i,const string&b,int index){
  const string& s=i.raw;const string& op=i.opcode;
  string rhs=i.lhs.empty()?s:s.substr(s.find(" = ")+3);
  if(op=="alloca")return;
  if(op=="phi")return;
  if(op=="load"){
   string addr=match1(rhs,R"(ptr\s+(%[\w.]+))");if(addr.empty())throw std::runtime_error("invalid load: "+s);
   auto reg=promoted.find(addr);
   if(reg!=promoted.end())emit("mv t0, "+reg->second);
   else loadRaw("t0",slot(addr));
   put(i.lhs,"t0");return;
  }
  if(op=="store"){
   std::smatch m;if(!std::regex_search(rhs,m,std::regex(R"(^store i32\s+([^,]+),\s*ptr\s+(%[\w.]+))")))throw std::runtime_error("invalid store: "+s);
   get(trim(m[1]),"t0");
   auto reg=promoted.find(m[2]);
   if(reg!=promoted.end())emit("mv "+reg->second+", t0");
   else storeRaw("t0",slot(m[2]));
   return;
  }
  if(op=="add"||op=="sub"||op=="mul"||op=="sdiv"||op=="srem"){
   auto p=splitArgs(rhs.substr(rhs.find("i32 ")+4));if(p.size()!=2)throw std::runtime_error("binary: "+s);
   string a=trim(p[0]),c=trim(p[1]);
   if(opt){
    auto value=[&](const string&v,int64_t &res){if(num(v)){res=i32(std::stoll(v));return true;}auto it=known.find(v);if(it!=known.end()){res=it->second;return true;}return false;};
    int64_t av=0,bv=0;
    if(value(a,av)&&value(c,bv)){
     bool ok=true;int64_t result=0;
     if(op=="add")result=i32(av+bv);
     else if(op=="sub")result=i32(av-bv);
     else if(op=="mul")result=i32(av*bv);
     else if(bv==0|| (av==INT32_MIN&&bv==-1))ok=false;
     else if(op=="sdiv")result=i32(av/bv);
     else result=i32(av%bv);
     if(ok){known[i.lhs]=result;return;}
    }
   }
   if(opt && num(c) && (op=="mul" || op=="srem")){
    int32_t divisor=i32(std::stoll(c));
    if(divisor>0 && (divisor & (divisor-1))==0){
     unsigned k=0;while((1ULL<<k)!=static_cast<unsigned>(divisor))++k;
     get(a,"t0");
     if(op=="mul")emit("slliw t2, t0, "+std::to_string(k));
     else if(k==0)emit("li t2, 0");
     else {
      // LLVM srem uses a quotient rounded toward zero, including negative dividends.
      // r = x - ((x + ((x < 0) ? (2^k-1) : 0)) & ~(2^k-1)).
      emit("sraiw t3, t0, 31");
      emit("srli t3, t3, "+std::to_string(64-k));
      emit("addw t3, t0, t3");
      emit("li t1, -"+std::to_string(divisor));
      emit("and t3, t3, t1");
      emit("subw t2, t0, t3");
     }
     put(i.lhs,"t2");return;
    }
   }
   get(a,"t0");get(c,"t1");
   string mn=op=="add"?"addw":op=="sub"?"subw":op=="mul"?"mulw":op=="sdiv"?"divw":"remw";
   emit(mn+" t2, t0, t1");put(i.lhs,"t2");return;
  }
  if(op=="icmp"){
   std::smatch m;if(!std::regex_search(rhs,m,std::regex(R"(^icmp\s+(\w+)\s+i32\s+([^,]+),\s*(\S+))")))throw std::runtime_error("invalid icmp "+s);
   string pred=m[1],a=m[2],c=m[3];get(a,"t0");get(c,"t1");
   if(pred=="eq"){emit("xor t2, t0, t1");emit("seqz t2, t2");}
   else if(pred=="ne"){emit("xor t2, t0, t1");emit("snez t2, t2");}
   else if(pred=="slt")emit("slt t2, t0, t1");
   else if(pred=="sgt")emit("slt t2, t1, t0");
   else if(pred=="sle"){emit("slt t2, t1, t0");emit("xori t2, t2, 1");}
   else if(pred=="sge"){emit("slt t2, t0, t1");emit("xori t2, t2, 1");}
   else throw std::runtime_error("unsupported icmp "+pred);
   put(i.lhs,"t2");return;
  }
  if(op=="zext"){
   string v=match1(rhs,R"(^zext i1\s+(\S+)\s+to i32)");if(v.empty())throw std::runtime_error("invalid zext");
   get(v,"t0");emit("andi t0, t0, 1");put(i.lhs,"t0");return;
  }
  if(op=="call"){
   std::smatch m;if(!std::regex_search(rhs,m,std::regex(R"(^call\s+(i32|void)\s+@([\w.]+)\((.*)\)$)")))throw std::runtime_error("call: "+s);
   string ret=m[1],fn=m[2];auto args=splitArgs(m[3]);
   // Staging arguments to stack before filling a-registers avoids overwriting argument sources.
   for(size_t n=0;n<args.size();++n){get(operand(args[n]),"t0");storeRaw("t0",outgoing+static_cast<int>(n)*8,true);}
   for(size_t n=0;n<args.size();++n){
    if(n<8)loadRaw("a"+std::to_string(n),outgoing+static_cast<int>(n)*8,true);
    else {loadRaw("t0",outgoing+static_cast<int>(n)*8,true);storeRaw("t0",static_cast<int>(n-8)*8,true);}
   }
   emit("call "+fn);lastSSA.clear();
   if(ret=="i32")put(i.lhs,"a0");
   return;
  }
  if(op=="br"){
   if(starts(rhs,"br label %")){go(b,trim(rhs.substr(10)));return;}
   std::smatch m;if(!std::regex_search(rhs,m,std::regex(R"(^br i1\s+(\S+),\s*label %(\S+),\s*label %(\S+))")))throw std::runtime_error("branch: "+s);
   string pred=m[1],t=m[2],f=m[3];
   if(opt && num(pred)){go(b,i32(std::stoll(pred))?t:f);return;}
   get(pred,"t0");
   bool trueHasPhi=phi.count(t)&&!phi.at(t).empty();
   string tramp=bb(b)+"_edge_"+std::to_string(index);
   emit("bnez t0, "+(trueHasPhi?tramp:bb(t)));
   go(b,f);
   if(trueHasPhi){label(tramp);go(b,t);}
   return;
  }
  if(op=="ret"){
   if(rhs!="ret void"){
    string val=match1(rhs,R"(^ret i32\s+(\S+))");if(val.empty())throw std::runtime_error("ret: "+s);
    get(val,"a0");
   }
   emit("j "+epilogue());return;
  }
  if(op=="unreachable"){emit("ebreak");return;}
  throw std::runtime_error("unsupported IR opcode: "+op);
 }
 void run(const Func&f){
  fname=f.name;slots.clear();known.clear();phi.clear();promoted.clear();promotedSSA.clear();calleeRegs.clear();lastSSA.clear();used=0;outgoing=0;
  int maxArgs=0,maxPhi=0;
  for(auto &b:f.blocks)for(auto&i:b.ins){
   if(i.opcode=="call"){
    string args=match1(i.raw,R"(@[\w.]+\((.*)\)$)");maxArgs=std::max(maxArgs,static_cast<int>(splitArgs(args).size()));
   }
   if(i.opcode=="phi")phi[b.name].push_back({i.lhs,i.raw});
  }
  for(auto &p:phi)maxPhi=std::max(maxPhi,static_cast<int>(p.second.size()));
  outgoing=round16(std::max(0,maxArgs-8)*8);
  // The first outgoing bytes are reserved for stack arguments; staging area starts after.
  int staging=outgoing;
  used=staging+std::max(1,maxArgs)*8;
  used=round16(used);
  auto alloc=[&](const string&name){if(!name.empty()&&!slots.count(name)){slots[name]=used;used+=8;}};
  for(auto&p:f.params)alloc(p);
  for(auto &b:f.blocks)for(auto&i:b.ins)alloc(i.lhs);
  if(opt){
   // Keep frequently-used, non-escaping local allocas in callee-saved registers.
   // Only take addresses that occur in an alloca, load or store. These addresses
   // are never passed by our ToyC IR generator, so no memory alias can observe them.
   unordered_map<string,int> freq;unordered_set<string> allocas;
   for(auto&b:f.blocks)for(auto&i:b.ins)if(i.opcode=="alloca")allocas.insert(i.lhs);
   for(auto&b:f.blocks)for(auto&i:b.ins){
    if(i.opcode=="load"||i.opcode=="store"){
     auto addr=match1(i.raw,R"(ptr\s+(%[\w.]+))");
     if(allocas.count(addr))++freq[addr];
    }
   }
   // SSA temporaries also use callee-saved registers when profitable.
   // Unlike a linear-scan allocator, this conservative assignment gives each
   // selected value its own register for its entire lifetime, so it cannot
   // introduce overlapping live ranges or clobber values across a call.
   unordered_set<string> ssaDefs;
   for(auto&p:f.params)ssaDefs.insert(p);
   for(auto&b:f.blocks)for(auto&i:b.ins)
    if(!i.lhs.empty() && i.opcode!="alloca")ssaDefs.insert(i.lhs);
   static const std::regex nameToken(R"(%[A-Za-z_][A-Za-z0-9_.]*)");
   for(auto&b:f.blocks)for(auto&i:b.ins){
    string rhs=i.lhs.empty()?i.raw:i.raw.substr(i.raw.find(" = ")+3);
    for(std::sregex_iterator it(rhs.begin(),rhs.end(),nameToken),end;it!=end;++it)
     if(ssaDefs.count(it->str()))freq[it->str()]+=1;
    if(i.opcode=="phi")freq[i.lhs]+=24;
   }
   vector<std::pair<string,int>> ranked(freq.begin(),freq.end());
   std::sort(ranked.begin(),ranked.end(),[](auto&a,auto&b){if(a.second!=b.second)return a.second>b.second;return a.first<b.first;});
   const char* names[]={"s1","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11"};
   for(size_t i=0;i<ranked.size()&&i<11;++i){
    promoted[ranked[i].first]=names[i];calleeRegs.push_back(names[i]);
    if(ssaDefs.count(ranked[i].first))promotedSSA.insert(ranked[i].first);
   }
  }
  int phiStage=used;used+=maxPhi*8;
  // phi staging must be separate from all regular slots.
  // branches() uses 'used' as staging base; assigned just below.
  frame=round16(used+16+static_cast<int>(calleeRegs.size())*8);
  savedS0=frame-16;savedRA=frame-8;
  savedRegsBase=frame-16-static_cast<int>(calleeRegs.size())*8;
  used=phiStage;
  out<<".text\n.globl "<<fname<<"\n.type "<<fname<<", @function\n";label(fname);
  if(frame<=2047)emit("addi sp, sp, -"+std::to_string(frame));
  else {emit("li t5, -"+std::to_string(frame));emit("add sp, sp, t5");}
  storeRaw("ra",savedRA,true);storeRaw("s0",savedS0,true);
  for(size_t i=0;i<calleeRegs.size();++i)
   storeRaw(calleeRegs[i],savedRegsBase+static_cast<int>(i)*8,true);
  emit("mv s0, sp");
  // Incoming a0..a7, extra arguments at old sp + 8*(index-8).
  for(size_t i=0;i<f.params.size();++i){
   if(i<8)put(f.params[i],"a"+std::to_string(i));
   else {loadRaw("t0",frame+static_cast<int>(i-8)*8,true);put(f.params[i],"t0");}
  }
  for(auto&b:f.blocks){
   lastSSA.clear();label(bb(b.name));
   // Fuse the common ToyC condition chain directly into an RV64 branch:
   // icmp i32 -> zext i1 to i32 -> icmp ne 0 -> br i1.
   for(size_t k=0;k<b.ins.size();++k){
    if(opt && k+3<b.ins.size() && b.ins[k].opcode=="icmp" &&
       b.ins[k+1].opcode=="zext" && b.ins[k+2].opcode=="icmp" &&
       b.ins[k+3].opcode=="br"){
      const auto&a=b.ins[k],&z=b.ins[k+1],&test=b.ins[k+2],&br=b.ins[k+3];
      std::smatch cmp,brm;
      string ir0=a.raw.substr(a.raw.find(" = ")+3);
      bool ok=std::regex_match(ir0,cmp,std::regex(R"(icmp (eq|ne|slt|sle|sgt|sge) i32 ([^,]+), (\S+))"));
      ok=ok && z.raw.find("zext i1 "+a.lhs+" to i32")!=string::npos;
      ok=ok && test.raw.find("icmp ne i32 "+z.lhs+", 0")!=string::npos;
      ok=ok && std::regex_match(br.raw,brm,std::regex(R"(br i1 (%[\w.]+), label %([\w.]+), label %([\w.]+))"));
      ok=ok && (brm.empty()||brm[1].str()==test.lhs);
      if(ok){
       // Single-use guards protect cases where the condition values are reused.
       auto uses=[&](const string&needle){
        int count=0;
        for(auto&bb2:f.blocks)for(auto&ii:bb2.ins){
         string rhs=ii.lhs.empty()?ii.raw:ii.raw.substr(ii.raw.find(" = ")+3);
         static const std::regex token(R"(%[A-Za-z_][A-Za-z0-9_.]*)");
         for(std::sregex_iterator it(rhs.begin(),rhs.end(),token),end;it!=end;++it)
          if(it->str()==needle)++count;
        }
        return count;
       };
       if(uses(a.lhs)==1&&uses(z.lhs)==1&&uses(test.lhs)==1){
        directBranch(b.name,static_cast<int>(k),cmp[1],trim(cmp[2]),trim(cmp[3]),brm[2],brm[3]);
        k+=3;continue;
       }
      }
    }
    inst(b.ins[k],b.name,static_cast<int>(k));
   }
  }
  label(epilogue());
  for(size_t i=0;i<calleeRegs.size();++i)
   loadRaw(calleeRegs[i],savedRegsBase+static_cast<int>(i)*8,true);
  loadRaw("ra",savedRA,true);loadRaw("s0",savedS0,true);
  if(frame<=2047)emit("addi sp, sp, "+std::to_string(frame));
  else {emit("li t5, "+std::to_string(frame));emit("add sp, sp, t5");}
  emit("ret");out<<".size "<<fname<<", .-"<<fname<<"\n\n";
 }
};
inline string emitAssembly(const string&ir,bool opt){
 auto funcs=parse(ir);Generator g;g.opt=opt;
 for(const auto&f:funcs)g.run(f);
 return g.out.str();
}
}
