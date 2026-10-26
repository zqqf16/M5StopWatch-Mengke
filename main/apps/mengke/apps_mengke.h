// 奇妙萌可之闪耀流星 apps
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>
#include <lvgl.h>

#define MK_APP_DECL(Cls)                                   \
    class Cls : public mooncake::AppAbility {             \
    public:                                                \
        Cls();                                             \
        ~Cls();                                            \
        void onCreate() override;                          \
        void onOpen() override;                            \
        void onRunning() override;                         \
        void onClose() override;                           \
        struct State;                                      \
                                                           \
    private:                                               \
        std::unique_ptr<input::KeyManager> _keys;          \
        lv_obj_t* _root = nullptr;                         \
        std::unique_ptr<State> _s;                         \
    };

MK_APP_DECL(AppMengkeCarousel)
MK_APP_DECL(AppMengkeDraw)
MK_APP_DECL(AppMengkeWatch)
MK_APP_DECL(AppMengkeQuiz)
MK_APP_DECL(AppMengkeCatch)
MK_APP_DECL(AppMengkeTransform)
MK_APP_DECL(AppMengkeMemory)
MK_APP_DECL(AppMengkePet)
MK_APP_DECL(AppMengkeSettings)
