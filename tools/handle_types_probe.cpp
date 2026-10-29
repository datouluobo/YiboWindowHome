#include <windows.h>
#include <winternl.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>
namespace {
struct Entry {PVOID Object;ULONG_PTR ProcessId;ULONG_PTR HandleValue;ULONG GrantedAccess;USHORT CreatorBackTraceIndex;USHORT ObjectTypeIndex;ULONG HandleAttributes;ULONG Reserved;};
struct Table {ULONG_PTR Count;ULONG_PTR Reserved;Entry Entries[1];};
using QuerySystem=NTSTATUS(NTAPI*)(ULONG,PVOID,ULONG,PULONG);
using QueryObject=NTSTATUS(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
struct TypeInfo {UNICODE_STRING name;ULONG stats[12];ULONG invalid;GENERIC_MAPPING mapping;ULONG valid;BOOLEAN security;BOOLEAN maintain;UCHAR index;UCHAR reserved;ULONG pool;ULONG paged;ULONG nonpaged;};
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;DWORD pid=wcstoul(argv[1],nullptr,10);if(!pid)return 2;
    HANDLE target=OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);bool canDuplicate=target!=nullptr;
    if(!target)target=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!target){std::cout<<"OpenProcess failed="<<GetLastError()<<'\n';return 2;}
    auto lib=GetModuleHandleW(L"ntdll.dll");auto query=reinterpret_cast<QuerySystem>(GetProcAddress(lib,"NtQuerySystemInformation"));auto objectQuery=reinterpret_cast<QueryObject>(GetProcAddress(lib,"NtQueryObject"));
    if(!query||!objectQuery){CloseHandle(target);return 2;}
    std::map<USHORT,std::wstring> systemTypes;std::vector<unsigned char> typeData(1024*1024);ULONG typeSize{};
    if(objectQuery(nullptr,3,typeData.data(),static_cast<ULONG>(typeData.size()),&typeSize)>=0){
        auto count=*reinterpret_cast<const ULONG*>(typeData.data());size_t pos=sizeof(void*);
        for(ULONG i=0;i<count&&pos+sizeof(TypeInfo)<=typeData.size();++i){auto type=reinterpret_cast<const TypeInfo*>(typeData.data()+pos);auto b=reinterpret_cast<const unsigned char*>(type->name.Buffer);
            if(b<typeData.data()||b+type->name.Length>typeData.data()+typeData.size()||type->name.Length>1024)break;
            if(type->index)systemTypes[type->index]=std::wstring(type->name.Buffer,type->name.Length/sizeof(wchar_t));
            pos=(size_t(b-typeData.data())+type->name.MaximumLength+sizeof(void*)-1)&~(sizeof(void*)-1);
        }
    }
    std::vector<unsigned char> data(1024*1024);ULONG needed{};NTSTATUS status{};
    for(int i=0;i<10;++i){status=query(64,data.data(),static_cast<ULONG>(data.size()),&needed);if(status>=0)break;if(static_cast<ULONG>(status)!=0xC0000004UL||data.size()>=256*1024*1024){CloseHandle(target);return 2;}data.resize(std::max<size_t>(data.size()*2,size_t(needed)+65536));}
    if(status<0){CloseHandle(target);return 2;}
    auto table=reinterpret_cast<const Table*>(data.data());size_t capacity=(data.size()-offsetof(Table,Entries))/sizeof(Entry);if(table->Count>capacity){CloseHandle(target);return 2;}
    struct Group {size_t count{};std::wstring name;DWORD duplicateError{};NTSTATUS queryStatus{};};std::map<USHORT,Group> groups;
    for(ULONG_PTR i=0;i<table->Count;++i){auto& entry=table->Entries[i];if(entry.ProcessId!=pid)continue;auto& group=groups[entry.ObjectTypeIndex];++group.count;if(!group.name.empty())continue;
        if(!canDuplicate)continue;
        HANDLE duplicate{};if(!DuplicateHandle(target,reinterpret_cast<HANDLE>(entry.HandleValue),GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)){group.duplicateError=GetLastError();continue;}
        std::vector<unsigned char> info(4096);ULONG length{};auto result=objectQuery(duplicate,2,info.data(),static_cast<ULONG>(info.size()),&length);group.queryStatus=result;CloseHandle(duplicate);
        if(result>=0){auto name=reinterpret_cast<const UNICODE_STRING*>(info.data());if(name->Buffer&&name->Length<=1024)group.name.assign(name->Buffer,name->Length/sizeof(wchar_t));}
    }
    DWORD count{};GetProcessHandleCount(target,&count);CloseHandle(target);std::wcout<<L"pid="<<pid<<L" process-handle-count="<<count<<L" can-duplicate="<<canDuplicate<<L"\n";
    for(auto& [id,group]:groups){auto type=group.name.empty()&&systemTypes.contains(id)?systemTypes[id]:group.name;std::wcout<<L"type-index="<<id<<L" type="<<(type.empty()?L"unknown":type)<<L" count="<<group.count<<L" duplicate-error="<<group.duplicateError<<L" query-status="<<static_cast<ULONG>(group.queryStatus)<<L"\n";}
    return 0;
}
