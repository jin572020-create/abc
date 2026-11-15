#pragma once
// Scope-aware semantic checks on the AST; errors are reported as 0-based lines.
class Semantic {
    struct Function {int argc; bool returnsVoid;};
    std::unordered_map<std::string,Function> funcs = {
        {"getint",{0,false}}, {"putint",{1,true}}, {"getch",{0,false}},
        {"putch",{1,true}}, {"getarray",{1,false}}, {"putarray",{2,true}}
    };
    std::vector<std::unordered_set<std::string>> scopes;
    std::vector<int> problems;
    int loopDepth=0;
    bool functionVoid=false;
    void issue(int line){problems.push_back(line);}
    bool declared(const std::string &name)const {
        for(auto it=scopes.rbegin();it!=scopes.rend();++it)if(it->count(name))return true;
        return false;
    }
    void declare(const Node &n){
        if(!scopes.back().insert(n->text).second)issue(n->line);
    }
    // True if expression has void type.
    bool expression(const Node &n){
        if(!n)return false;
        if(n->type=="Number")return false;
        if(n->type=="Variable"){
            if(!declared(n->text))issue(n->line);
            return false;
        }
        if(n->type=="Call"){
            for(auto &a:n->children)if(expression(a))issue(a->line);
            auto it=funcs.find(n->text);
            if(it!=funcs.end()){
                if(it->second.argc!=(int)n->children.size())issue(n->line);
                return it->second.returnsVoid;
            }
            issue(n->line);return false;
        }
        for(auto &c:n->children)if(expression(c))issue(c->line);
        return false;
    }
    void stmt(const Node &n){
        if(!n)return;
        if(n->type=="Block"){
            scopes.emplace_back();
            for(auto &child:n->children)stmt(child);
            scopes.pop_back();return;
        }
        if(n->type=="Decl"||n->type=="ConstDecl"){
            declare(n);
            if(!n->children.empty() && expression(n->children[0]))issue(n->line);
            return;
        }
        if(n->type=="Assign"){
            if(!declared(n->text))issue(n->line);
            if(expression(n->children[0]))issue(n->line);
            return;
        }
        if(n->type=="Break"||n->type=="Continue"){
            if(loopDepth==0)issue(n->line);
            return;
        }
        if(n->type=="While"){
            if(expression(n->children[0]))issue(n->line);
            ++loopDepth;stmt(n->children[1]);--loopDepth;return;
        }
        if(n->type=="If"){
            if(expression(n->children[0]))issue(n->line);
            stmt(n->children[1]);if(n->children.size()>2)stmt(n->children[2]);return;
        }
        if(n->type=="Return"){
            if(!n->children.empty()){
                bool isVoid=expression(n->children[0]);
                if(isVoid||functionVoid)issue(n->line);
            } else if(!functionVoid)issue(n->line);
            return;
        }
        if(n->type=="ExprStmt"){
            if(!n->children.empty())expression(n->children[0]);
            return;
        }
    }
public:
    std::vector<int> check(const Node &root){
        // Collect function signatures before checking bodies (forward calls allowed).
        for(const auto &f:root->children){
            int argc=(int)f->children.size()-2;
            bool isVoid=f->children[0]->text=="void";
            if(funcs.count(f->text))issue(f->line);
            funcs[f->text]={argc,isVoid};
        }
        for(const auto &f:root->children){
            scopes.clear();scopes.emplace_back();
            functionVoid=f->children[0]->text=="void";
            loopDepth=0;
            for(size_t i=1;i+1<f->children.size();++i)declare(f->children[i]);
            // Function's outermost block shares scope with its parameters.
            auto b=f->children.back();
            for(const auto &s:b->children)stmt(s);
        }
        return problems;
    }
};
