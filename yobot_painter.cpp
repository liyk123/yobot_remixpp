#include "yobot_painter.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <memory>
#include <fstream>

auto SDLSurfaceDeleter = [](SDL_Surface* surface) {
    if (surface)
    {
        SDL_DestroySurface(surface);
    }
};

auto SDLIOStreamDeleter = [](SDL_IOStream* stream) {
    if (stream)
    {
        SDL_CloseIO(stream);
    }
};

bool CreateAndSaveImage_CPP()
{
    const int width = 256;
    const int height = 256;
    const auto format = SDL_PIXELFORMAT_RGBA8888;

    std::unique_ptr<SDL_Surface, decltype(SDLSurfaceDeleter)> surface = {
        SDL_CreateSurface(width, height, format),
        SDLSurfaceDeleter
    };

    SDL_Surface* s_ptr = surface.get();

    if (s_ptr && SDL_LockSurface(s_ptr))
    {
        Uint32* pixels = static_cast<Uint32*>(s_ptr->pixels);
        const int stride_pixels = s_ptr->pitch / sizeof(Uint32);

        // 绘制一个简单的渐变
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                // 计算 RGBA 分量
                Uint8 r = static_cast<Uint8>(x * 255 / width);
                Uint8 g = static_cast<Uint8>(y * 255 / height);
                Uint8 b = 128;
                Uint8 a = 255;

                // 将 RGBA 颜色值映射到 Surface 的像素格式 (Uint32)
                std::uint32_t color = SDL_MapSurfaceRGBA(s_ptr, r, g, b, a);

                // 写入像素
                pixels[y * stride_pixels + x] = color;
            }
        }
        SDL_UnlockSurface(s_ptr);
        std::unique_ptr<SDL_IOStream, decltype(SDLIOStreamDeleter)> sdlStream = { 
            SDL_IOFromDynamicMem(),
            SDLIOStreamDeleter 
        };
        if (sdlStream && SDL_SaveBMP_IO(s_ptr, sdlStream.get(), false))
        {
            std::cout << SDL_GetIOSize(sdlStream.get()) << std::endl;
            std::byte buffer[1024] = {};
            std::ofstream ofile("test.bmp", std::ios::binary);
            std::size_t si = 0;
            SDL_SeekIO(sdlStream.get(), 0, SDL_IO_SEEK_SET);
            while (si = SDL_ReadIO(sdlStream.get(), buffer, sizeof(buffer)))
            {
                std::cout << si << std::endl;
                ofile.write((char*)buffer, sizeof(buffer));
                memset(buffer, 0, sizeof(buffer));
            }
            std::cout << SDL_GetIOStatus(sdlStream.get()) << std::endl;
            return true;
        }
    }
    std::cout << SDL_GetError() << std::endl;
    return false;
}

namespace yobot {
    painter::painter()
    {
        SDL_Init(SDL_INIT_VIDEO);
    }
    painter::~painter()
    {
        SDL_Quit();
    }
    painter& painter::getInstance()
    {
        static painter instance{};
        return instance;
    }
    void painter::draw()
    {
        CreateAndSaveImage_CPP();
    }
}
