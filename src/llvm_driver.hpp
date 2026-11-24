#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <iterator>

// LLVM/Clang backend: runs the LLVM IR optimizer and RISC-V64GC instruction selector,
// register allocator and assembler emitter. No shell expansion of user input.
inline std::string compileLLVM(const std::string &ir,bool asmMode,bool optimized){
    char inname[]="/tmp/toyc_ir_XXXXXX";
    char outname[]="/tmp/toyc_output_XXXXXX";
    int infd=mkstemp(inname);
    if(infd<0)throw std::runtime_error("unable to create temporary IR file");
    int outfd=mkstemp(outname);
    if(outfd<0){close(infd);unlink(inname);throw std::runtime_error("unable to create temporary output file");}
    try{
        // write loop handles short writes
        size_t off=0;
        while(off<ir.size()){
            ssize_t n=write(infd,ir.data()+off,ir.size()-off);
            if(n<=0)throw std::runtime_error("cannot write IR temporary file");
            off+=static_cast<size_t>(n);
        }
        close(infd);infd=-1;close(outfd);outfd=-1;
        const char *candidate=getenv("TOYC_CLANG");
        std::string clang=(candidate&&*candidate)?candidate:"clang";
        std::vector<std::string> args={clang,"-x","ir","-S"};
        args.push_back("-target");args.push_back("riscv64-unknown-elf");
        args.push_back("-march=rv64gc");args.push_back("-mabi=lp64d");
        if(!asmMode)args.push_back("-emit-llvm");
        args.push_back(optimized?"-O2":"-O0");
        args.push_back(inname);args.push_back("-o");args.push_back(outname);
        std::vector<char*> argv;for(auto &a:args)argv.push_back(a.data());argv.push_back(nullptr);
        pid_t pid=fork();if(pid<0)throw std::runtime_error("fork() failed");
        if(pid==0){execvp(clang.c_str(),argv.data());
            const char *msg="ToyC: clang executable not found (configure TOYC_CLANG)\n";
            write(STDERR_FILENO,msg,strlen(msg));_exit(127);}
        int status=0;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)throw std::runtime_error("waitpid() failed");}
        if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)
            throw std::runtime_error("LLVM backend failed, exit code "+std::to_string(WIFEXITED(status)?WEXITSTATUS(status):-1));
        std::ifstream file(outname,std::ios::binary);
        if(!file)throw std::runtime_error("LLVM output not created");
        std::string output((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        unlink(inname);unlink(outname);return output;
    }catch(...){if(infd>=0)close(infd);if(outfd>=0)close(outfd);unlink(inname);unlink(outname);throw;}
}
