#pragma once
// Included from main.cpp after Token is defined, inside the anonymous namespace.
struct AST {
    std::string type, text;
    int line = 0;
    std::vector<std::shared_ptr<AST>> children;
};
using Node = std::shared_ptr<AST>;
inline Node node(const std::string &type, const Token &t, std::vector<Node> children={}) {
    auto n=std::make_shared<AST>(); n->type=type; n->text=t.lexeme; n->line=t.line; n->children=std::move(children); return n;
}
struct ParseError {};
class Parser {
    std::vector<Token> ts;
    size_t p=0;
    std::vector<int> problems;
    Token eofToken;
    const Token &peek(size_t off=0)const { return p+off<ts.size()?ts[p+off]:eofToken; }
    bool is(const char *x)const {return peek().lexeme==x;}
    bool id() const {return peek().kind=="Ident";}
    bool number()const {return peek().kind=="IntConst";}
    Token take(){Token t=peek();if(p<ts.size())++p;return t;}
    bool match(const char *s){if(is(s)){take();return true;}return false;}
    [[noreturn]] void fail() { problems.push_back(peek().line); throw ParseError{}; }
    Token expect(const char *s){if(!is(s))fail();return take();}
    Token ident(){if(!id())fail();return take();}
    bool startsExpr() const{return id()||number()||is("(")||is("+")||is("-")||is("!");}
    Node primary(){
        if(number()){auto t=take();return node("Number",t);}
        if(id()) {
            auto t=take();
            if(match("(")) {
                std::vector<Node> args;
                if(!is(")")) {args.push_back(expr());while(match(","))args.push_back(expr());}
                expect(")"); return node("Call",t,std::move(args));
            }
            return node("Variable",t);
        }
        if(match("(")){auto n=expr();expect(")");return n;}
        fail();
    }
    Node unary(){if(is("+")||is("-")||is("!")){auto t=take();return node("Unary",t,{unary()});}return primary();}
    Node mul(){auto n=unary();while(is("*")||is("/")||is("%")){auto t=take();n=node("Binary",t,{n,unary()});}return n;}
    Node add(){auto n=mul();while(is("+")||is("-")){auto t=take();n=node("Binary",t,{n,mul()});}return n;}
    Node rel(){auto n=add();while(is("<")||is(">")||is("<=")||is(">=")||is("==")||is("!=")){auto t=take();n=node("Binary",t,{n,add()});}return n;}
    Node land(){auto n=rel();while(is("&&")){auto t=take();n=node("Binary",t,{n,rel()});}return n;}
    Node lor(){auto n=land();while(is("||")){auto t=take();n=node("Binary",t,{n,land()});}return n;}
    Node expr(){return lor();}
    Node stmt(){
        if(is("{"))return block();
        if(is(";")){auto t=take();return node("Empty",t);}
        if(is("int")||is("const")) {
            auto t=take(); bool c=t.lexeme=="const";
            if(c)expect("int");
            auto name=ident(); Node init;
            if(match("="))init=expr();
            else if(c)fail();
            expect(";"); return node(c?"ConstDecl":"Decl",name,init?std::vector<Node>{init}:std::vector<Node>{});
        }
        if(is("if")) {
            auto t=take();expect("(");auto cond=expr();expect(")");auto then=stmt();
            std::vector<Node> children={cond,then};if(match("else"))children.push_back(stmt());
            return node("If",t,std::move(children));
        }
        if(is("while")) {
            auto t=take();
            if(!is("(") && id() && peek(1).lexeme=="=") problems.push_back(peek().line);
            expect("(");auto cond=expr();expect(")");auto body=stmt();return node("While",t,{cond,body});
        }
        if(is("break")||is("continue")){auto t=take();expect(";");return node(t.lexeme=="break"?"Break":"Continue",t);}
        if(is("return")){auto t=take();Node value;if(!is(";"))value=expr();expect(";");return node("Return",t,value?std::vector<Node>{value}:std::vector<Node>{});}
        if(id() && peek(1).lexeme=="=") {
            auto name=take();take();auto e=expr();expect(";");return node("Assign",name,{e});
        }
        if(startsExpr()){auto e=expr();expect(";");return node("ExprStmt",peek(),{e});}
        fail();
    }
    void recover(size_t begin){
        // Panic-mode synchronization: never hang on malformed statements.
        // A malformed compound statement can contain its own braces.
        // Skip that whole nested block instead of consuming the parent's closing brace.
        if(is("else"))take();
        if(is("{")) {
            int depth=0;
            do {if(is("{"))++depth;else if(is("}"))--depth;take();}
            while(depth>0 && p<ts.size());
            return;
        }
        if(p==begin && p<ts.size() && !is("}"))++p;
        while(p<ts.size()) {
            if(match(";"))return;
            if(is("}")||is("if")||is("while")||is("return")||is("int")||is("const")||is("void"))return;
            ++p;
        }
    }
    Node block(){
        auto t=expect("{");auto b=node("Block",t);
        while(p<ts.size() && !is("}")) {
            auto before=p;
            try {b->children.push_back(stmt());}
            catch(const ParseError&) {recover(before);if(p==before && p<ts.size() && !is("}"))++p;}
        }
        expect("}");return b;
    }
    Node function(){
        if(!is("int")&&!is("void"))fail();
        auto type=take();auto name=ident();
        auto f=node("Function",name);f->text=name.lexeme;f->children.push_back(node("ReturnType",type));
        expect("(");
        if(!is(")")) {
            do {expect("int");auto x=ident();f->children.push_back(node("Param",x));} while(match(","));
        }
        expect(")");f->children.push_back(block());return f;
    }
public:
    explicit Parser(std::vector<Token> input,int lastLine):ts(std::move(input)) {
        eofToken={"EOF","<eof>",lastLine,0};
        if(!ts.empty())eofToken.line=std::max(lastLine,ts.back().line);
    }
    Node parse(){
        auto root=std::make_shared<AST>();root->type="Program";
        if(ts.empty())problems.push_back(0);
        while(p<ts.size()){
            auto before=p;
            try{root->children.push_back(function());}
            catch(const ParseError&){
                // No valid top-level statement except a function definition.
                if(p==before && p<ts.size())++p;
                while(p<ts.size() && !is("int") && !is("void"))++p;
            }
        }
        return root;
    }
    const std::vector<int>& errors()const{return problems;}
};
