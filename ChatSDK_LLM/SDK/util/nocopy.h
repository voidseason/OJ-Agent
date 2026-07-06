#ifndef NOCOPY_H
#define NOCOPY_H

namespace ChatSDK::nocopy
{
    /**
    * @brief 不可复制类
    * 
    */
    class nocopy
    {
    public:
        nocopy() = default;
        ~nocopy() = default;
        nocopy(const nocopy&) = delete;
        nocopy& operator=(const nocopy&) = delete;
    };
}
#endif