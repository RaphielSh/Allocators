#include <iostream>
#include <chrono>
#include <string.h>
using namespace std;

// Exceptions

class Exception:public exception{
    protected:
        char* str;//указатель хранящий сообщение об исключении
    public:
        Exception(const char* s){
            str = new char[strlen(s) + 1];//выделяем память для str, достаточную для хранения строки s
            strcpy_s(str, strlen(s) + 1, s);//копируем содержимое s в str
        }
        Exception(const Exception& e){//конструктор копирования
            str = new char[strlen(e.str) + 1];
            strcpy_s(str, strlen(e.str) + 1, e.str);
        }
        ~Exception(){
            delete[]str;//очищаем память выделенную для str
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
            cout << "Not Allocated: " << str << "\nArg: " << Arg << endl;
        }
};

using OutOfBounds = Exception; // делаем OOB "синонимом" Exception


// Paddings
size_t CalculatePadding(size_t baseAddress, size_t alignment){
    size_t multiplier = (baseAddress / alignment) + 1; //Вычисления множителя. Мы округляем в большую сторону количество раз, на которое alignment может быть помещено в базовый адрес
    size_t alignedAddress = multiplier * alignment; //Выровненный адрес. Он следующим после базового адреса. Он будет больше или равен ему, а также кратен alignment.
    size_t padding = alignedAddress - baseAddress; // Разница между выровленным адресом и базовым адресом
    return padding;
}

size_t CalculatePaddingWithHeader(size_t baseAddress, size_t alignment, size_t headerSize){  // Функция вычисления заполнения с учетом заголовка
    size_t padding = CalculatePadding(baseAddress, alignment); // Исходное количество байтов для базового заполнения, не учитывая заголовок
    size_t neededSpace = headerSize; // Количество байтов, нужное для заголовка

    if(padding < neededSpace){ //если заполнение меньше необходимого места, то заголовок не помещается в текущее выравнивание
        neededSpace -= padding; //мы вычитаем из заголовка текущий padding, чтобы узнать сколько не вмещается
        if(neededSpace % alignment > 0){  //если neededSpace не кратен alignment, то нужно добавить дополнительный блок
            padding += alignment * (1 + (neededSpace / alignment)); //neededSpace / alignment - вычисляет количество полных блоков, нужных для умещения
        }
        else padding += alignment * (neededSpace / alignment); // если neededSpace кратен, то блоков хватает и остается только расширить padding
    }
    return padding;
}



// Base Allocator class

class Allocator{
    protected:
        uintptr_t  Base; // Address that allocates for allocator
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
class LinearAllocator: public Allocator{
    protected:
        size_t Offset = 0;
        size_t SizeOfRegion = 0;
    public: 

        LinearAllocator():
            Allocator(){
                Offset = 0; 
                SizeOfRegion = 0;
        }

        void Create(size_t AllocSize){
            SizeOfRegion = AllocSize;
            Allocator::Allocate(SizeOfRegion);
        }

        void* Allocate(size_t Size, size_t alignment = sizeof(int)){ // если при вызове Allocate не будет указан alignment, то он будет равен size_t
                                                                        // size_t равен 4 байтам на x32 и 8 на x64
           size_t Padding = 0;

           if (!Base) return nullptr;
           if(alignment != 0 && Offset % alignment != 0) Padding = CalculatePadding(Offset + Base, alignment); // Если смещение не кратно выравниванию, то нужно вычислить Padding
           if(Offset + Size + Padding > SizeOfRegion){ // текущее смещение + размер текущего блока + заполнение
               throw OutOfBounds("Out of bounds!");
               return nullptr;
            }

            Offset += Padding; // Если padding не 0, значит мы добавляли дополнительное место
            uintptr_t CurrentBlock = Offset + Base;
            Offset += Size; 

            return (void*)CurrentBlock;// конвертация из uintptr в void*, чтобы вернуть указатель на выделенную память без опр. типа данных
        }

        void Free(){   // Освобождение
            Offset = 0;
        }

        void Destroy(){
            Offset = 0;
            SizeOfRegion = 0;
            Allocator::Destroy();
        }

        ~LinearAllocator(){}
};

class StackAllocator: public LinearAllocator{
    public:
        
        StackAllocator():LinearAllocator(){}

        void Create(size_t AllocSize){
            SizeOfRegion = AllocSize;
            Allocator::Allocate(SizeOfRegion);
        }

        void* Allocate(size_t Size, size_t alignment = 8){
            
            if(!Base) return nullptr;

            uintptr_t CurrentBlock = Base + Offset;
            size_t padding = CalculatePaddingWithHeader(CurrentBlock, alignment, sizeof(size_t));
            
            if(Offset + padding + Size > SizeOfRegion){
                throw OutOfBounds("Out of bounds");
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

        ~StackAllocator(){}
};

// in ms Timer
long long GetEpochTime(){
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
}

int main(){


    cout << "=============Linear Allocator test===============" << endl;


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
        catch(OutOfBounds e){
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
        catch(OutOfBounds e){
            e.print();
        }
        if (!A) break;
        *A = i;
        if (i >= 25) Stack.Deallocate((void*)A);
        cout << "Allocated address: " << hex << A << " | Block № " << dec << *A << endl;
    }
    Stack.Destroy();



    cout << "=============Time tests===============" << endl;



    LinearAllocator LinearTest;
    long long StartTime = GetEpochTime();
    try{
        LinearTest.Create(sizeof(int) * 500000);
    }
    catch(AllocationException e){
        e.print();
    }
    for (int i = 0; i < 500000; i++){
        try{
            LinearTest.Allocate(sizeof(int), 0);
        }
        catch(OutOfBounds e){
            e.print();
            break;
        }
    }
    LinearTest.Free();
    long long EndTime = GetEpochTime();
    cout << "Linear Allocator time test(500 000 ops): " << EndTime - StartTime << " ms" << endl;


    StackAllocator StackTest;
    StartTime = GetEpochTime();
    try{
        StackTest.Create(sizeof(int) * 500000);
    }
    catch(AllocationException e){
        e.print();
    }
    for(int i = 0; i < 500000; i++){
        try{
            StackTest.Allocate(sizeof(int));
        }
        catch(OutOfBounds e){
            e.print();
            break;
        }
    }

    StackTest.Free();
    EndTime = GetEpochTime();
    cout << "Stack Allocator time test(500 000 ops): " << EndTime - StartTime << " ms" << endl;
    

    system("pause");
    return 0;
}











    // cout << "=============Exception Test===============" << endl;


    // LinearAllocator LinearTestException;
    // try{
    //     LinearTestException.Create(numeric_limits <uintptr_t>::max());
    // }
    // catch(AllocationException e){
    //     e.print();
    // }
    // LinearTestException.Destroy();
    // try{
    //     LinearTestException.Create(1000);
    // }
    // catch(AllocationException e){
    //     e.print();
    // }
    // try{
    //     LinearTestException.Allocate(1001);
    // }
    // catch(OutOfBounds e){
    //     e.print();
    // }
