#pragma once
namespace yobot {
    class painter
    {
    private:
        painter();
    public:
        painter(painter&&) = default;
        painter(painter&) = default;
        ~painter();
        static painter& getInstance();
    public:
        void draw();
    };
}

