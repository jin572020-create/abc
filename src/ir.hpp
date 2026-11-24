#pragma once
// ToyC AST -> LLVM IR (opaque pointers); all local variables use entry-block allocas.
// This header is included after parser.hpp and semantic.hpp in main.cpp.
class IREmitter {
    struct BB {
        std::string name;
        std::vector<std::string> lines;
        bool terminated=false;
    };
    struct FunctionSig {std::string result; size_t argc;};
    std::unordered_map<std::string,FunctionSig> signatures;
    std::vector<BB> blocks;
    std::vector<std::string> allocas;
    std::vector<std::unordered_map<std::string,std::string>> scopes;
    std::vector<std::pair<std::string,std::string>> loops; // continue,break
    size_t current=0;
    int tempCount=0, blockCount=0;
    std::string resultType="i32";
    bool terminated() const {return blocks[current].terminated;}
    std::string tmp(){return "%t"+std::to_string(tempCount++);}
    std::string label(const std::string &prefix){return prefix+std::to_string(blockCount++);}
    void emit(const std::string &s){if(terminated())throw std::runtime_error("IR: emit after terminator");blocks[current].lines.push_back("  "+s);}
    void term(const std::string &s){emit(s);blocks[current].terminated=true;}
    size_t create(const std::string &name){blocks.push_back({name,{ },false});return blocks.size()-1;}
    void activate(size_t b){current=b;}
    std::string target(size_t b) const{return "%"+blocks[b].name;}
    void jump(size_t b){term("br label "+target(b));}
    void conditional(const std::string &pred,size_t a,size_t b){term("br i1 "+pred+", label "+target(a)+", label "+target(b));}
    void push(){scopes.emplace_back();}
    void pop(){scopes.pop_back();}
    std::string lookup(const std::string &s) const{
        for(auto i=scopes.rbegin();i!=scopes.rend();++i){auto j=i->find(s);if(j!=i->end())return j->second;}
        throw std::runtime_error("IR: undeclared variable: "+s);
    }
    std::string declare(const std::string &name){
        std::string p=tmp();allocas.push_back("  "+p+" = alloca i32, align 4");
        if(!scopes.back().emplace(name,p).second)throw std::runtime_error("IR: duplicate variable: "+name);
        return p;
    }
    std::string binary(const std::string &op,const std::string &lhs,const std::string &rhs){
        std::string name=tmp();
        std::string opname;
        if(op=="+")opname="add";
        else if(op=="-")opname="sub";
        else if(op=="*")opname="mul";
        else if(op=="/")opname="sdiv";
        else if(op=="%")opname="srem";
        else throw std::runtime_error("IR: unsupported binary op: "+op);
        emit(name+" = "+opname+" i32 "+lhs+", "+rhs);
        return name;
    }
    std::string boolval(const Node &n){
        size_t yes=create(label("bool.true")), no=create(label("bool.false")), end=create(label("bool.end"));
        emitCond(n,yes,no);
        activate(yes);jump(end);
        activate(no);jump(end);
        activate(end);
        std::string v=tmp();emit(v+" = phi i32 [ 1, "+target(yes)+" ], [ 0, "+target(no)+" ]");
        return v;
    }
    std::string eval(const Node &n){
        if(n->type=="Number"){
            // Enforce i32 wraparound of integer literals.
            unsigned long long v=0;
            for(char c:n->text){if(c<'0'||c>'9')break;v=(v*10+static_cast<unsigned>(c-'0'))&0xFFFFFFFFULL;}
            return std::to_string(static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(v))));
        }
        if(n->type=="Variable"){
            std::string v=tmp();emit(v+" = load i32, ptr "+lookup(n->text)+", align 4");return v;
        }
        if(n->type=="Call"){
            auto it=signatures.find(n->text);
            if(it==signatures.end())throw std::runtime_error("IR: unknown function: "+n->text);
            std::vector<std::string> args;
            for(auto &a:n->children)args.push_back("i32 "+eval(a)); // preserves left-to-right evaluation
            std::string str;
            for(size_t i=0;i<args.size();++i){if(i)str+=", ";str+=args[i];}
            if(it->second.result=="void"){
                emit("call void @"+n->text+"("+str+")");return "";
            }
            std::string v=tmp();emit(v+" = call i32 @"+n->text+"("+str+")");return v;
        }
        if(n->type=="Unary"){
            if(n->text=="!")return boolval(n);
            std::string a=eval(n->children.at(0));
            if(n->text=="+")return a;
            if(n->text=="-")return binary("-","0",a);
            throw std::runtime_error("IR: invalid unary");
        }
        if(n->type=="Binary"){
            std::string op=n->text;
            if(op=="&&"||op=="||")return boolval(n);
            std::string a=eval(n->children.at(0));
            std::string b=eval(n->children.at(1));
            if(op=="+"||op=="-"||op=="*"||op=="/"||op=="%")return binary(op,a,b);
            std::string cmp;
            if(op=="==")cmp="eq";else if(op=="!=")cmp="ne";
            else if(op=="<")cmp="slt";else if(op=="<=")cmp="sle";
            else if(op==">")cmp="sgt";else if(op==">=")cmp="sge";
            else throw std::runtime_error("IR: invalid compare op");
            std::string t=tmp(),r=tmp();emit(t+" = icmp "+cmp+" i32 "+a+", "+b);
            emit(r+" = zext i1 "+t+" to i32");return r;
        }
        throw std::runtime_error("IR: invalid expr node: "+n->type);
    }
    void emitCond(const Node &e,size_t yes,size_t no){
        if(e->type=="Unary"&&e->text=="!") {emitCond(e->children.at(0),no,yes);return;}
        if(e->type=="Binary"&&e->text=="&&"){
            size_t rhs=create(label("and.rhs"));emitCond(e->children.at(0),rhs,no);
            activate(rhs);emitCond(e->children.at(1),yes,no);return;
        }
        if(e->type=="Binary"&&e->text=="||"){
            size_t rhs=create(label("or.rhs"));emitCond(e->children.at(0),yes,rhs);
            activate(rhs);emitCond(e->children.at(1),yes,no);return;
        }
        std::string v=eval(e),t=tmp();
        emit(t+" = icmp ne i32 "+v+", 0");conditional(t,yes,no);
    }
    void stmt(const Node &n){
        if(!n||terminated())return;
        if(n->type=="Block"){
            push();for(const auto &c:n->children){if(terminated())break;stmt(c);}pop();return;
        }
        if(n->type=="Decl"||n->type=="ConstDecl"){
            std::string ptr=declare(n->text);
            if(!n->children.empty()){auto v=eval(n->children.at(0));emit("store i32 "+v+", ptr "+ptr+", align 4");}
            return;
        }
        if(n->type=="Assign"){
            auto v=eval(n->children.at(0));emit("store i32 "+v+", ptr "+lookup(n->text)+", align 4");return;
        }
        if(n->type=="ExprStmt"){
            if(!n->children.empty())eval(n->children[0]);
            return;
        }
        if(n->type=="Empty")return;
        if(n->type=="Return"){
            if(n->children.empty())term("ret void");
            else {auto v=eval(n->children.at(0));term("ret i32 "+v);}return;
        }
        if(n->type=="Break"){
            if(loops.empty())throw std::runtime_error("IR: break outside loop");
            term("br label %"+loops.back().second);return;
        }
        if(n->type=="Continue"){
            if(loops.empty())throw std::runtime_error("IR: continue outside loop");
            term("br label %"+loops.back().first);return;
        }
        if(n->type=="If"){
            size_t yes=create(label("if.then")),no=create(label("if.else")),end=create(label("if.end"));
            emitCond(n->children.at(0),yes,no);
            activate(yes);stmt(n->children.at(1));bool liveYes=!terminated();if(liveYes)jump(end);
            activate(no);if(n->children.size()>2)stmt(n->children.at(2));bool liveNo=!terminated();if(liveNo)jump(end);
            activate(end);
            if(!liveYes&&!liveNo)term("unreachable");
            return;
        }
        if(n->type=="While"){
            size_t head=create(label("while.head")),body=create(label("while.body")),end=create(label("while.end"));
            jump(head);activate(head);emitCond(n->children.at(0),body,end);
            loops.push_back({blocks[head].name,blocks[end].name});
            activate(body);stmt(n->children.at(1));if(!terminated())jump(head);
            loops.pop_back();activate(end);return;
        }
        throw std::runtime_error("IR: unsupported statement: "+n->type);
    }
    void function(const Node &f,std::ostringstream &out){
        blocks.clear();allocas.clear();scopes.clear();loops.clear();tempCount=0;blockCount=0;
        current=create("entry");push();
        resultType=f->children.front()->text=="void"?"void":"i32";
        std::vector<std::string> params;
        for(size_t i=1;i+1<f->children.size();++i){
            params.push_back("i32 %arg"+std::to_string(i-1));
            std::string ptr=declare(f->children[i]->text);
            emit("store i32 %arg"+std::to_string(i-1)+", ptr "+ptr+", align 4");
        }
        // The outermost function body shares scope with the parameters.
        for(const auto &c:f->children.back()->children){if(terminated())break;stmt(c);}
        if(!terminated()){
            if(resultType=="void")term("ret void");
            else throw std::runtime_error("IR: reachable end without return in int function: "+f->text);
        }
        pop();
        out<<"define "<<resultType<<" @"<<f->text<<"(";
        for(size_t i=0;i<params.size();++i){if(i)out<<", ";out<<params[i];}
        out<<") {\n";
        for(size_t i=0;i<blocks.size();++i){
            out<<blocks[i].name<<":\n";
            if(i==0)for(const auto &a:allocas)out<<a<<"\n";
            for(const auto &line:blocks[i].lines)out<<line<<"\n";
            if(!blocks[i].terminated)throw std::runtime_error("IR: unterminated block: "+blocks[i].name);
        }
        out<<"}\n\n";
    }
public:
    std::string generate(const Node &ast){
        signatures.clear();
        signatures={{"getint",{"i32",0}},{"putint",{"void",1}},
                    {"getch",{"i32",0}},{"putch",{"void",1}},
                    {"getarray",{"i32",1}},{"putarray",{"void",2}}};
        for(const auto &f:ast->children)
            signatures[f->text]={f->children.front()->text=="void"?"void":"i32",f->children.size()-2};
        std::ostringstream out;
        out<<"; ModuleID = 'toyc'\nsource_filename = \"toyc\"\n"
              "target triple = \"riscv64-unknown-unknown-elf\"\n\n";
        for(const auto &kv:signatures){
            bool own=false;for(const auto &f:ast->children)if(f->text==kv.first)own=true;
            if(own)continue;
            out<<"declare "<<kv.second.result<<" @"<<kv.first<<"(";
            for(size_t i=0;i<kv.second.argc;++i){if(i)out<<", ";out<<"i32";}
            out<<")\n";
        }
        out<<"\n";
        for(const auto &f:ast->children)function(f,out);
        return out.str();
    }
};
