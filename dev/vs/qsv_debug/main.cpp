#include <stdint.h>
#include <memory>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <chrono>

#include "callback.h"
#include "common.h"
#include "system.h"

using namespace std;

extern "C" void hwcodec_log(int level, const char* message) {
    std::cout << message << std::endl;
}

extern "C" int ffmpeg_vram_test_encode(void* outDescs, int32_t maxDescNum,
    int32_t* outDescNum, API api, DataFormat dataFormat,
    int32_t width, int32_t height, int32_t kbs,
    int32_t framerate, int32_t gop);


int main() {
    AdapterDesc descs[1];
    int counter = 0;
    while (true) {
        int32_t outDescNum = 0;
        ffmpeg_vram_test_encode(descs, 1, &outDescNum, API_DX11, H265, 1920, 1080, 1000, 30, 0xFFFF);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::cout << counter++ << std::endl;
    }

	return 0;
}