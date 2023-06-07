#include <iostream>
#include <chrono>
#include <string.h>
using namespace std;

// Exceptions

class Exception:public exception{
    protected:
        char* str;
    public:
        Exception(const char* s){
            str = new char[strlen(s) + 1];//выделяем память для str, достаточную для хранения строки s
            strcpy_s(str, strlen(s) + 1, s);//копируем содержимое s в str
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
        size_t error_size;
    public:
        AllocationException(size_t error_size, const char* s): 
            Exception(s), error_size(error_size){};
        AllocationException(size_t error_size, const Exception& e):
            Exception(e), error_size(error_size){};
        AllocationException(const AllocationException& e):
            Exception(e.str), error_size(e.error_size){};
        virtual void print(){
            cout << "Not Allocated: " << str << "\nSize that coused error: " << error_size << endl;
        }
};

using Out_Of_Bounds = Exception; // делаем OOB "синонимом" Exception


// Paddings
size_t Add_Padding(size_t base_address, size_t alignment){
    size_t multiplier = (base_address / alignment) + 1; //Вычисления множителя. Мы округляем в большую сторону количество раз, на которое alignment может быть помещено в базовый адрес
    size_t aligned_address = multiplier * alignment; //Выровненный адрес. Он следующим после базового адреса. Он будет больше или равен ему, а также кратен alignment.
    size_t padding = aligned_address - base_address; // Разница между выровленным адресом и базовым адресом
    return padding;
}

size_t Add_Padding_Stack(size_t base_address, size_t alignment, size_t headerSize){  // Функция вычисления заполнения с учетом заголовка
    size_t padding = Add_Padding(base_address, alignment); // Исходное количество байтов для базового заполнения, не учитывая заголовок
    size_t required_space = headerSize; // Количество байтов, нужное для заголовка

    if(padding < required_space){ //если заполнение меньше необходимого места, то заголовок не помещается в текущее выравнивание
        required_space -= padding; //мы вычитаем из заголовка текущий padding, чтобы узнать сколько не вмещается
        if(required_space % alignment > 0){  //если required_space не кратен alignment, то нужно добавить дополнительный блок
            padding += alignment * (1 + (required_space / alignment)); //required_space / alignment - вычисляет количество полных блоков, нужных для умещения
        }
        else padding += alignment * (required_space / alignment); // если required_space кратен, то блоков хватает и остается только расширить padding
    }
    return padding;
}

// Base Allocator class

class Allocator{
    protected:
        uintptr_t  Base;
    public: 

        Allocator(){
            Base = 0;
        }

        void* Allocate(size_t Size){

            Base = (uintptr_t)malloc(Size);

            if(!Base) throw AllocationException(Size, "allocation exception");

            return(void*)Base;
        }
        void Destroy(){
            if(Base){
                free((void*)Base);
                Base = 0;
            }
        }

        ~Allocator(){
            Destroy();
        }
};
class Linear_Allocator: public Allocator{
    protected:
        size_t offset = 0;
        size_t region_size = 0;
    public: 

        Linear_Allocator():
            Allocator(){
                offset = 0; 
                region_size = 0;
        }

        void Create(size_t AllocSize){
            region_size = AllocSize;
            Allocator::Allocate(region_size);
        }

        void* Allocate(size_t Size, size_t alignment = sizeof(int)){

           size_t Padding = 0;

           if (!Base) return nullptr;
           if(alignment != 0 && offset % alignment != 0) Padding = Add_Padding(offset + Base, alignment); // Если смещение не кратно выравниванию, то нужно вычислить Padding
           if(offset + Size + Padding > region_size){ // текущее смещение + размер текущего блока + заполнение
               throw Out_Of_Bounds("Out of bounds!");
               return nullptr;
            }

            offset += Padding; // Если padding не 0, значит мы добавляли дополнительное место
            uintptr_t CurrentBlock = offset + Base;
            offset += Size; 

            return (void*)CurrentBlock;// конвертация из uintptr в void*, чтобы вернуть указатель на выделенную память без опр. типа данных
        }

        void Free(){   // Освобождение
            offset = 0;
        }

        void Destroy(){
            offset = 0;
            region_size = 0;
            Allocator::Destroy();
        }

        ~Linear_Allocator(){}
};

class StackAllocator: public Linear_Allocator{
    public:
        
        StackAllocator():Linear_Allocator(){}

        void Create(size_t AllocSize){
            region_size = AllocSize;
            Allocator::Allocate(region_size);
        }

        void* Allocate(size_t Size, size_t alignment = 4){
            
            if(!Base) return nullptr;

            uintptr_t CurrentBlock = Base + offset;
            size_t padding = Add_Padding_Stack(CurrentBlock, alignment, sizeof(int));
            
            if(offset + padding + Size > region_size){
                throw Out_Of_Bounds("Out of bounds");
                return nullptr;
            }

            offset += padding;
            uintptr_t NextBlock = CurrentBlock + padding;
            *(int*)(NextBlock - sizeof(int)) = padding;
            offset += Size;
            return (void*)NextBlock;
        
        }

        void Deallocate(void* pointer){ 
            offset = (uintptr_t)pointer - *(int*)((uintptr_t)pointer - sizeof(int)) - Base;
        }

        ~StackAllocator(){}
};

// in ms Timer
long long GetEpochTime(){
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
}



int main(){


    cout << "=============Linear Allocator test===============" << endl;


    Linear_Allocator Linear;
    try{
        Linear.Create(sizeof(int) * 50);
    }
    catch(AllocationException e){
        e.print();
    }
    for(int i = 0; i < 50; i++){
        int* A = 0;
        try{
            A = (int*)Linear.Allocate(sizeof(int));
        }
        catch(Out_Of_Bounds e){
            e.print();
        }
        if(!A) break;
        *A = i;
        cout << "Allocated address: " << hex << A << " | Block №" << dec << *A << endl;
    }
    Linear.Destroy();


    cout << "=============Stack Allocator test===============" << endl;


    StackAllocator Stack;
    try{
        Stack.Create(sizeof(int) * 100);
    }
    catch(AllocationException e){
        e.print();
    }
    for (int i = 0; i < 50; i++){
        int *A = 0;
        try{
            A = (int*)Stack.Allocate(sizeof(int));
        }
        catch(Out_Of_Bounds e){
            e.print();
        }
        if (!A) break;
        *A = i;
        if (i >= 25) Stack.Deallocate((void*)A);
        cout << "Allocated address: " << hex << A << " | Block № " << dec << *A << endl;
    }
    Stack.Destroy();



    cout << "=============Time tests===============" << endl;



    Linear_Allocator Linear_Time_Test;
    long long StartTime = GetEpochTime();
    try{
        Linear_Time_Test.Create(sizeof(int) * 500000);
    }
    catch(AllocationException e){
        e.print();
    }
    for (int i = 0; i < 500000; i++){
        try{
            Linear_Time_Test.Allocate(sizeof(int), 0);
        }
        catch(Out_Of_Bounds e){
            e.print();
            break;
        }
    }
    Linear_Time_Test.Free();
    long long EndTime = GetEpochTime();
    cout << "Linear Allocator time test(500 000 ops): " << EndTime - StartTime << " ms" << endl;


    StackAllocator Stack_Time_Test;
    StartTime = GetEpochTime();
    try{
        Stack_Time_Test.Create(sizeof(int) * 1000000);
    }
    catch(AllocationException e){
        e.print();
    }
    for(int i = 0; i < 500000; i++){
        try{
            Stack_Time_Test.Allocate(sizeof(int));
        }
        catch(Out_Of_Bounds e){
            e.print();
            break;
        }
    }

    Stack_Time_Test.Free();
    EndTime = GetEpochTime();
    cout << "Stack Allocator time test(500 000 ops): " << EndTime - StartTime << " ms" << endl;
    

    cout << "=============Exception Test===============" << endl;


    Linear_Allocator Linear_Test_Exception;
    try{
        Linear_Test_Exception.Create(numeric_limits <uintptr_t>::max());
    }
    catch(AllocationException e){
        e.print();
    }
    Linear_Test_Exception.Destroy();
    try{
        Linear_Test_Exception.Create(1000);
    }
    catch(AllocationException e){
        e.print();
    }
    try{
        Linear_Test_Exception.Allocate(1001);
    }
    catch(Out_Of_Bounds e){
        e.print();
    }

    return 0;
}


