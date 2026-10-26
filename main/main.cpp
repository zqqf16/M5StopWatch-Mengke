/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <lv_demos.h>
#include <apps/common/audio/audio.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install apps: 奇妙萌可之闪耀流星 fan firmware (launcher, key handling and power key from the official UserDemo)
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppMengkeCarousel>());
    GetMooncake().installApp(std::make_unique<AppMengkeDraw>());
    GetMooncake().installApp(std::make_unique<AppMengkeWatch>());
    GetMooncake().installApp(std::make_unique<AppMengkeQuiz>());
    GetMooncake().installApp(std::make_unique<AppMengkeCatch>());
    GetMooncake().installApp(std::make_unique<AppMengkeTransform>());
    GetMooncake().installApp(std::make_unique<AppMengkeMemory>());
    GetMooncake().installApp(std::make_unique<AppMengkePet>());
    GetMooncake().installApp(std::make_unique<AppMengkeSettings>());

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetMooncake().update();
    }
}
