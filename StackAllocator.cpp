#include <iostream>
#include <chrono>
#include <string.h>
using namespace std;

size_t CalculatePadding(size_t baseAddress, size_t alignment){
    size_t multiplier = (baseAddress / alignment) + 1;
    size_t alignedAddress = multiplier * alignment;
    size_t padding = alignedAddress - baseAddress;
    return padding;
}

size_t CalculatePaddingWithHeader(size_t baseAddress, size_t alignment, size_t headerSize){
    size_t padding = CalculatePadding(baseAddress, alignment);
    size_t neededSpace = headerSize;

    if(padding < neededSpace){
        neededSpace -= padding;
        if(neededSpace % alignment > 0){
            padding =+ alignment * (1 + (neededSpace / alignment));
        }
        else padding += alignment * (neededSpace / alignment);       //Possible crash
    }
    //else padding += alignment * (neededSpace / alignment);
    return padding;
}
class Exception:
public exception{
    protected:
    char* str;
    public:
    Exception(const char* s){
        str = new char[strlen(s) + 1];
        strcpy_s(str, strlen(s) + 1, s);
    }
    Exception(const Exception& e){
        str = new char[strlen(e.str) + 1];
        strcpy_s(str, strlen(e.str) + 1, e.str);
    }
    ~Exception(){
        delete[]str;
    }
    virtual void print(){
        cout << "Exception: " << str << endl;
    }
};

class AllocationException: public Exception{
    private:
    size_t Arg;
    public:
    AllocationException(size_t Arg, const char* s): 
    Exception(s), Arg(Arg){};
    AllocationException(size_t Arg, const Exception& e):
    Exception(e), Arg(Arg){};
    AllocationException(const AllocationException& e):
    Exception(e.str), Arg(e.Arg){};
    virtual void print(){
        cout << "Allocation Exception: " << str << "\nArg: " << Arg << endl;
    }
};
using OutOfBoundsException = Exception; // No need of new ranges, so we'll use base Allocator class

class Allocator{
    protected:
    uintptr_t  Base; // Address that allocates for allocator
    public: Allocator(){
        Base = 0;
    }
    ~Allocator(){
        Destroy();
    }
    void* Allocate(size_t Size){
        Base = (uintptr_t)malloc(Size);
        if(!Base) throw AllocationException(Size, "Allocation Exception");
        return(void*)Base;
    }
    void Destroy(){
        if(Base){
            free((void*)Base);
            Base = 0;
        }
    }
};
class LinearAllocator: public Allocator{
    protected:
    size_t Offset = 0;
    size_t SizeOfRegion = 0;
    public: LinearAllocator():
    Allocator(){
        Offset = 0; 
        SizeOfRegion = 0;
    }
    ~LinearAllocator(){}
    void Create(size_t AllocSize){
        SizeOfRegion = AllocSize;
        Allocator::Allocate(SizeOfRegion);
    }
    void* Allocate(size_t Size, size_t alignment = sizeof(size_t)){
        if (!Base) return nullptr;
        size_t Padding = 0;
        if(alignment != 0 && Offset % alignment != 0) Padding = CalculatePadding(Offset + Base, alignment);
        if(Offset + Size + Padding > SizeOfRegion){
            throw OutOfBoundsException("Out of bounds exception");
            return nullptr;
        }
        Offset += Padding;
        uintptr_t CurrentBlock = Offset + Base;
        Offset += Size;
        return (void*)CurrentBlock;
    }
    void Free(){
        Offset = 0;
    }
    void Destroy(){
        Offset = 0;
        SizeOfRegion = 0;
        Allocator::Destroy();
    }
};

class StackAllocator: public LinearAllocator{
    public:
    StackAllocator():LinearAllocator(){}
    ~StackAllocator(){}
    void Create(size_t AllocSize){
        SizeOfRegion = AllocSize;
        Allocator::Allocate(SizeOfRegion);
    }
    void* Allocate(size_t Size, size_t alignment = 8){
        if(!Base) return nullptr;
        uintptr_t CurrentBlock = Base + Offset;
        size_t padding = CalculatePaddingWithHeader(CurrentBlock, alignment, sizeof(size_t));
        if(Offset + padding + Size > SizeOfRegion){
            throw OutOfBoundsException("Out of bounds exception");
            return nullptr;
        }
        Offset += padding;
        uintptr_t NextBlock = CurrentBlock + padding;
        *(size_t*)(NextBlock - sizeof(size_t)) = padding;
        Offset += Size;
        return (void*)NextBlock;
    }
    void Deallocate(void* Ptr){
        Offset = (uintptr_t)Ptr - *(size_t*)((uintptr_t)Ptr - sizeof(size_t)) - Base;
    }
};

long long GetEpochTime(){
    return std::chrono::duration_cast <std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
}

int main(){
    LinearAllocator Linear;
    try{
        Linear.Create(sizeof(int) * 100);
    }
    catch(AllocationException e){
        e.print();
    }
    for(int i = 0; i < 100; i++){
        int* A = 0;
        try{
            A = (int*)Linear.Allocate(sizeof(int));
        }
        catch(OutOfBoundsException e){
            e.print();
        }
        if(!A) break;
        *A = i;
        cout << "Allocated address: 0x" << hex << A << "value stored in address: " << dec << *A << endl;
    }
    Linear.Destroy();
    StackAllocator Stack;
    try{
        Stack.Create(sizeof(int) * 100);
    }
    catch(AllocationException e){
        e.print();
    }
    for (int i = 0; i < 50; i++){
        int* A = 0;
        try{
            A = (int*)Stack.Allocate(sizeof(int));
        }
        catch(OutOfBoundsException e){
            e.print();
        }
        if (!A) break;
        *A = i;
        if (i >= 25) Stack.Deallocate((void*)A);
        cout << "Allocated address: 0x" << hex << A << "value stored in address: " << dec << *A << endl;
    }
    Stack.Destroy();
    

    LinearAllocator LinearTest;
    long long StartTime = GetEpochTime();
    try{
        LinearTest.Create(sizeof(int) * 100000);
    }
    catch(AllocationException e){
        e.print();
    }
    for (int i = 0; i < 100000; i++){
        try{
            LinearTest.Allocate(sizeof(int), 0);
        }
        catch(OutOfBoundsException e){
            e.print();
            break;
        }
    }
    LinearTest.Free();
    long long EndTime = GetEpochTime();
    cout << "Linear Allocator, 100000 operations done in: " << EndTime - StartTime << " ms" << endl;

    StackAllocator StackTest;
    StartTime = GetEpochTime();
    try{
        StackTest.Create(sizeof(int) * 200001);
    }
    catch(AllocationException e){
        e.print();
    }
    for(int i = 0; i < 100000; i++){
        try{
            StackTest.Allocate(sizeof(int));
        }
        catch(OutOfBoundsException e){
            e.print();
            break;
        }
    }
    StackTest.Free();
    EndTime = GetEpochTime();
    cout << "Stack Allocator, 100000 operations dine in: " << EndTime - StartTime << " ms" << endl;
    
    LinearAllocator LinearTestException;
    try{
        LinearTestException.Create(numeric_limits <uintptr_t>::max());
    }
    catch(AllocationException e){
        e.print();
    }
    LinearTestException.Destroy();
    try{
        LinearTestException.Create(1000);
    }
    catch(AllocationException e){
        e.print();
    }
    try{
        LinearTestException.Allocate(1001);
    }
    catch(OutOfBoundsException e){
        e.print();
    }
    system("pause");
    return 0;
}