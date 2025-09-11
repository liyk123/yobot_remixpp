#include <mimalloc-new-delete.h>
#include <mimalloc-override.h>
#include "yobot.h"

//void test()
//{
//    long long num = 123456789LL;
//    std::cout << std::format(std::locale(TargetLocaleName), "{:L}\n", num) << std::endl;;
//}

int main(int argc, char** args)
{
	static volatile int MI_VERSION = mi_version();
    yobot::initialize();
    yobot::start();
    //test();
    return 0;
}
