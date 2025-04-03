/*创建一个设备类,内部有输入值数据成员并提供获取这些数据的API,
比如鼠标状态和鼠标移动和鼠标位置,其他对象会调用接口获取数据,
此外提供事件绑定*/
#pragma once

namespace Device{
    class device
    {
    private:
        /* data */
    public:
        device(/* args */);
        ~device();
    };
    
}