#include <memory>


namespace GWS {
    class BufferBuilder
    {
    private:
        std::pair<unsigned char*, size_t> segment[64];
    public:
        BufferBuilder(size_t capacity);
        ~BufferBuilder();
        void TakeOver();

    };
    

    
}
