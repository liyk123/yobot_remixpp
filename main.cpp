#include <mimalloc-new-delete.h>
#include <mimalloc-override.h>
#include "yobot.h"

int main(int argc, char** args)
{
    static volatile int MI_VERSION = mi_version();
    yobot::initialize();
    yobot::start();
    //yobot::test();
    return 0;
}
