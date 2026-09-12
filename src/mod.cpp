#include <algorithm>
#include <cstdint>

#include "d/dolzel.h"
#include "d/d_msg_object.h"
#include "d/d_msg_scrn_3select.h"
#include "d/d_msg_scrn_light.h"
#include "d/d_msg_scrn_talk.h"
#include "d/d_select_cursor.h"
#include "d/d_pane_class.h"
#include "JSystem/J2DGraph/J2DScreen.h"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

DEFINE_MOD();

IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_OPTIONAL_SERVICE(ConfigService, svc_config);
IMPORT_OPTIONAL_SERVICE(UiService, svc_ui);

static ConfigVarHandle s_dialogueScale = 0;
static ConfigVarHandle s_dialogueOpacity = 0;
static ConfigVarHandle s_choiceScale = 0;
static ConfigVarHandle s_choiceOpacity = 0;

static int64_t g_dialogueScale = 100;
static int64_t g_dialogueOpacity = 100;
static int64_t g_choiceScale = 100;
static int64_t g_choiceOpacity = 100;

static constexpr int kScaleMin = 25;
static constexpr int kScaleMax = 150;
static constexpr int kOpacityMin = 0;
static constexpr int kOpacityMax = 100;

static void onDialogueScale(ModContext*, ConfigVarHandle, const ConfigVarValue* value,
                             const ConfigVarValue*, void*) {
    if (value != nullptr) {
        g_dialogueScale = std::clamp<int64_t>(value->int_value, kScaleMin, kScaleMax);
    }
}

static void onDialogueOpacity(ModContext*, ConfigVarHandle, const ConfigVarValue* value,
                               const ConfigVarValue*, void*) {
    if (value != nullptr) {
        g_dialogueOpacity = std::clamp<int64_t>(value->int_value, kOpacityMin, kOpacityMax);
    }
}

static void onChoiceScale(ModContext*, ConfigVarHandle, const ConfigVarValue* value,
                          const ConfigVarValue*, void*) {
    if (value != nullptr) {
        g_choiceScale = std::clamp<int64_t>(value->int_value, kScaleMin, kScaleMax);
    }
}

static void onChoiceOpacity(ModContext*, ConfigVarHandle, const ConfigVarValue* value,
                            const ConfigVarValue*, void*) {
    if (value != nullptr) {
        g_choiceOpacity = std::clamp<int64_t>(value->int_value, kOpacityMin, kOpacityMax);
    }
}

static float dialogueScale() { return static_cast<float>(g_dialogueScale) / 100.0f; }
static float dialogueOpacity() { return static_cast<float>(g_dialogueOpacity) / 100.0f; }
static float choiceScale() { return static_cast<float>(g_choiceScale) / 100.0f; }
static float choiceOpacity() { return static_cast<float>(g_choiceOpacity) / 100.0f; }

static void scaleHierarchyAroundCenter(CPaneMgr* paneMgr, float factor) {
    if (paneMgr == nullptr || factor <= 0.0f || factor == 1.0f) {
        return;
    }
    J2DPane* pane = paneMgr->getPanePtr();
    if (pane == nullptr) {
        return;
    }
    const Vec centerBefore = paneMgr->getGlobalVtxCenter(false, 0);
    pane->scale(pane->getScaleX() * factor, pane->getScaleY() * factor);
    const Vec centerAfter = paneMgr->getGlobalVtxCenter(false, 0);
    const float dx = centerBefore.x - centerAfter.x;
    const float dy = centerBefore.y - centerAfter.y;
    if (dx != 0.0f || dy != 0.0f) {
        pane->move(pane->getBounds().i.x + dx, pane->getBounds().i.y + dy);
    }
}

static void setHierarchyScaleAroundCenter(CPaneMgr* paneMgr, float factor) {
    if (paneMgr == nullptr || factor <= 0.0f) {
        return;
    }
    J2DPane* pane = paneMgr->getPanePtr();
    if (pane == nullptr) {
        return;
    }
    const float baseX = paneMgr->getInitScaleX();
    const float baseY = paneMgr->getInitScaleY();
    const Vec centerBefore = paneMgr->getGlobalVtxCenter(false, 0);
    pane->scale(baseX * factor, baseY * factor);
    const Vec centerAfter = paneMgr->getGlobalVtxCenter(false, 0);
    const float dx = centerBefore.x - centerAfter.x;
    const float dy = centerBefore.y - centerAfter.y;
    if (dx != 0.0f || dy != 0.0f) {
        pane->move(pane->getBounds().i.x + dx, pane->getBounds().i.y + dy);
    }
}

struct SelectScreenLayout {
    void* vtable;
    J2DScreen* mpScreen;
    void* mpAnmBck;
    void* mpAnmBpk;
    void* mpAnmBtk;
    void* mpSelectCursor;
    CPaneMgr* mpParent;
    CPaneMgr* mpTmSel[3];
    CPaneMgr* mpTmrSel[3];
    CPaneMgr* mpSel[3];
    CPaneMgr* mpSelCld[3];
    CPaneMgr* mpSelCldr[3];
    CPaneMgr* mpSelCldm[3];
    CPaneMgr* mpSelCldw[5][3];
    CPaneMgr* mpSelPo[3];
    CPaneMgr* mpKahen[3];
    CPaneMgr* mpCursor[3];
    CPaneMgr* mpTouchArea[3];
};

static SelectScreenLayout* selectLayout(dMsgScrn3Select_c* screen) {
    return reinterpret_cast<SelectScreenLayout*>(screen);
}

DEFINE_HOOK(&dMsgScrnTalk_c::exec, TalkExec);
DEFINE_HOOK(&dMsgScrnTalk_c::drawSelf, TalkDrawSelf);
DEFINE_HOOK(&dMsgScrnTalk_c::fukiAlpha, TalkFukiAlpha);
DEFINE_HOOK(&dMsgScrnLight_c::drawCommon, LightDrawCommon);
DEFINE_HOOK(&dMsgScrn3Select_c::draw, SelectDraw);
DEFINE_HOOK(&dSelect_cursor_c::update, SelectCursorUpdate);
DEFINE_HOOK(&dMsgScrn3Select_c::draw, SelectDrawPost);

static dMsgScrnTalk_c* s_activeTalkScreen = nullptr;

static HookAction onTalkDrawSelfPre(ModContext*, void* args, void*, void*) {
    s_activeTalkScreen = mods::arg<dMsgScrnTalk_c*>(args, 0);
    return HOOK_CONTINUE;
}

static void onTalkDrawSelfPost(ModContext*, void*, void*, void*) {
    s_activeTalkScreen = nullptr;
}

static void onTalkExecPost(ModContext*, void* args, void*, void*) {
    auto* screen = mods::arg<dMsgScrnTalk_c*>(args, 0);
    if (screen == nullptr || screen->mpPmP_c == nullptr || screen->mpMg_c[1] == nullptr) {
        return;
    }

    scaleHierarchyAroundCenter(screen->mpPmP_c, dialogueScale());

    const float baseX = screen->mpMg_c[1]->getInitScaleX();
    const float baseY = screen->mpMg_c[1]->getInitScaleY();
    screen->mpMg_c[1]->scale(baseX * dialogueScale(), baseY * dialogueScale());
}

static void onFukiAlphaPost(ModContext*, void* args, void*, void*) {
    auto* screen = mods::arg<dMsgScrnTalk_c*>(args, 0);
    if (screen == nullptr || screen->mpPmP_c == nullptr) {
        return;
    }
    const float alpha = mods::arg<float>(args, 1);
    screen->mpPmP_c->setAlphaRate(alpha * g_MsgObject_HIO_c.mBoxTalkAlphaP * dialogueOpacity());
}

static HookAction onLightDrawCommonPre(ModContext*, void* args, void*, void*) {
    if (s_activeTalkScreen == nullptr || s_activeTalkScreen->mpMg_c[1] == nullptr) {
        return HOOK_CONTINUE;
    }
    const float factor = dialogueScale();
    if (factor <= 0.0f || factor == 1.0f) {
        return HOOK_CONTINUE;
    }
    float& x = mods::arg_ref<float>(args, 1);
    float& y = mods::arg_ref<float>(args, 2);
    float& scaleX = mods::arg_ref<float>(args, 3);
    float& scaleY = mods::arg_ref<float>(args, 4);
    const Vec center = s_activeTalkScreen->mpMg_c[1]->getGlobalVtxCenter(false, 0);
    x = center.x + (x - center.x) * factor;
    y = center.y + (y - center.y) * factor;
    scaleX *= factor;
    scaleY *= factor;
    return HOOK_CONTINUE;
}

static dSelect_cursor_c* s_activeDialogueChoiceCursor = nullptr;

static void onSelectCursorUpdatePost(ModContext*, void* args, void*, void*) {
    auto* cursor = mods::arg<dSelect_cursor_c*>(args, 0);
    if (cursor == nullptr || cursor != s_activeDialogueChoiceCursor) {
        return;
    }
    cursor->setScale(choiceScale());
}

static HookAction onSelectDrawPre(ModContext*, void* args, void*, void*) {
    s_activeDialogueChoiceCursor = nullptr;
    auto* screen = mods::arg<dMsgScrn3Select_c*>(args, 0);
    if (screen == nullptr) {
        return HOOK_CONTINUE;
    }
    auto* layout = selectLayout(screen);
    if (layout->mpParent == nullptr || layout->mpScreen == nullptr) {
        return HOOK_CONTINUE;
    }
    s_activeDialogueChoiceCursor = reinterpret_cast<dSelect_cursor_c*>(layout->mpSelectCursor);

    const float scale = choiceScale();
    setHierarchyScaleAroundCenter(layout->mpParent, scale);

    const float alpha = choiceOpacity();
    for (int i = 0; i < 3; ++i) {
        if (layout->mpSelCld[i] != nullptr) layout->mpSelCld[i]->setAlphaRate(alpha);
        if (layout->mpSelCldr[i] != nullptr) layout->mpSelCldr[i]->setAlphaRate(alpha);
        if (layout->mpSelCldm[i] != nullptr) layout->mpSelCldm[i]->setAlphaRate(alpha);
        if (layout->mpKahen[i] != nullptr) layout->mpKahen[i]->setAlphaRate(alpha);
        for (int j = 0; j < 5; ++j) {
            if (layout->mpSelCldw[j][i] != nullptr) layout->mpSelCldw[j][i]->setAlphaRate(alpha);
        }
    }
    return HOOK_CONTINUE;
}

static void onSelectDrawPost(ModContext*, void*, void*, void*) {
    s_activeDialogueChoiceCursor = nullptr;
}

static void registerInt(const char* name, int64_t def, ConfigVarHandle* handle,
                        void (*callback)(ModContext*, ConfigVarHandle, const ConfigVarValue*,
                                         const ConfigVarValue*, void*)) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_INT;
    desc.default_int = def;
    if (svc_config->register_var(mod_ctx, &desc, handle) != MOD_OK) {
        return;
    }
    int64_t value = def;
    if (svc_config->get_int(mod_ctx, *handle, &value) == MOD_OK && callback) {
        ConfigVarValue v{};
        v.int_value = value;
        callback(mod_ctx, *handle, &v, nullptr, nullptr);
    }
    if (callback) {
        svc_config->subscribe(mod_ctx, *handle, callback, nullptr, nullptr);
    }
}

static void resetDefaults(ModContext*, void*) {
    if (!svc_config) return;
    svc_config->set_int(mod_ctx, s_dialogueScale, 100);
    svc_config->set_int(mod_ctx, s_dialogueOpacity, 100);
    svc_config->set_int(mod_ctx, s_choiceScale, 100);
    svc_config->set_int(mod_ctx, s_choiceOpacity, 100);
}

static void addNumberControl(UiElementHandle panel, const char* label, ConfigVarHandle var,
                             int min, int max) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_NUMBER;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = var;
    desc.min = min;
    desc.max = max;
    desc.step = 5;
    desc.suffix = "%";
    svc_ui->pane_add_control(mod_ctx, panel, &desc, nullptr);
}

static ModResult buildUi(ModContext*, UiElementHandle panel, void*, ModError*) {
    if (!svc_ui) return MOD_OK;

    svc_ui->pane_add_section(mod_ctx, panel, "Dialogue Box");
    addNumberControl(panel, "Scale", s_dialogueScale, kScaleMin, kScaleMax);
    addNumberControl(panel, "Opacity", s_dialogueOpacity, kOpacityMin, kOpacityMax);

    svc_ui->pane_add_section(mod_ctx, panel, "Dialogue Choices");
    addNumberControl(panel, "Scale", s_choiceScale, kScaleMin, kScaleMax);
    addNumberControl(panel, "Opacity", s_choiceOpacity, kOpacityMin, kOpacityMax);

    UiControlDesc reset = UI_CONTROL_DESC_INIT;
    reset.kind = UI_CONTROL_BUTTON;
    reset.label = "Reset to defaults";
    reset.on_pressed = resetDefaults;
    svc_ui->pane_add_control(mod_ctx, panel, &reset, nullptr);

    return MOD_OK;
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    if (!svc_hook) return mods::set_error(error, MOD_ERROR, "HookService unavailable");

    if (svc_config) {
        registerInt("dialogueScale", 100, &s_dialogueScale, onDialogueScale);
        registerInt("dialogueOpacity", 100, &s_dialogueOpacity, onDialogueOpacity);
        registerInt("choiceScale", 100, &s_choiceScale, onChoiceScale);
        registerInt("choiceOpacity", 100, &s_choiceOpacity, onChoiceOpacity);
    }

    if (svc_ui) {
        UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
        panel.build = buildUi;
        svc_ui->register_mods_panel(mod_ctx, &panel);
    }

    ModResult result = mods::hook::add_post<TalkExec>(svc_hook, onTalkExecPost);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dMsgScrnTalk_c::exec");

    result = mods::hook::add_pre<TalkDrawSelf>(svc_hook, onTalkDrawSelfPre);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dMsgScrnTalk_c::drawSelf pre");

    result = mods::hook::add_post<TalkDrawSelf>(svc_hook, onTalkDrawSelfPost);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dMsgScrnTalk_c::drawSelf post");

    result = mods::hook::add_post<TalkFukiAlpha>(svc_hook, onFukiAlphaPost);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dMsgScrnTalk_c::fukiAlpha");

    result = mods::hook::add_pre<LightDrawCommon>(svc_hook, onLightDrawCommonPre);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dMsgScrnLight_c::drawCommon");

    result = mods::hook::add_pre<SelectDraw>(svc_hook, onSelectDrawPre);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dMsgScrn3Select_c::draw");

    result = mods::hook::add_post<SelectCursorUpdate>(svc_hook, onSelectCursorUpdatePost);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dSelect_cursor_c::update");

    result = mods::hook::add_post<SelectDrawPost>(svc_hook, onSelectDrawPost);
    if (result != MOD_OK) return mods::set_error(error, result, "Failed to hook dMsgScrn3Select_c::draw post");

    svc_log->info(mod_ctx, "Dialogue Options v1.0.1 initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) { return MOD_OK; }

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    s_activeTalkScreen = nullptr;
    s_activeDialogueChoiceCursor = nullptr;
    return MOD_OK;
}
}

extern "C" MOD_EXPORT const void* const g_keep_mod_records[] = {
    &mod_meta_header_record,
    &mod_meta_import_svc_hook,
    &mod_meta_import_svc_log,
    &mod_meta_import_svc_config,
    &mod_meta_import_svc_ui,
};
