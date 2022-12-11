//此代码,测试timeNode,与timeQueue的使用
#include <iostream>
#include "tinyasync/tinyasync.h"

using namespace tinyasync;


//定义多个timeNodes
timeNode mynodes[30];

void sleep_seconds(int secs){
    for(int i=1;i<=secs;++i){
        std::cout << "sleep " << i << " seconds" << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(){
    std::cout << "test time_queue" << "\n";

    // 创建timeQueue,超时3s
    timeQueue<3> myque;

    myque.push(&mynodes[0]);

    auto __now = Clock::now();
    //是否超时
    std::cout << myque.front() -> is_expire(__now)<< "\n";
    sleep_seconds(4); //等待4s

    //4s 后是否超时
    __now = Clock::now();
    std::cout << myque.front() -> is_expire(__now)<< "\n";

    //删除第一个元素,是否变空
    std::cout << "empty : "  << myque.empty()<< "\n";
    myque.front()->remove_self();
    std::cout << "empty : "  << myque.empty()<< "\n";

    //测试,每一个元素自己删除,是否会变空
    for(int i=1;i<=10;++i){
        myque.push(&mynodes[i]);
    }

    std::cout << "empty : "  << myque.empty()<< "\n";
    for(int i=1;i<=9;++i){
        mynodes[i].remove_self();
    }
    std::cout << "empty : "  << myque.empty()<< "\n";
    mynodes[10].remove_self();
    std::cout << "empty : "  << myque.empty()<< "\n";



    return 0;
}
