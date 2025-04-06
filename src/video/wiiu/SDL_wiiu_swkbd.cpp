/*
  Simple DirectMedia Layer
  Copyright (C) 2025 Daniel K. O. <dkosmari@gmail.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <array>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "SDL_wiiu_swkbd.h"

#include <coreinit/filesystem.h>
#include <coreinit/mcp.h>
#include <coreinit/userconfig.h>
#include <nn/swkbd.h>

#include "SDL_stdinc.h"
#include "SDL_system.h"
#include "SDL_syswm.h"

#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"

using namespace std::string_literals;
using nn::swkbd::RegionType;
using nn::swkbd::LanguageType;

// #define SDL_WIIU_DONT_REPLACE_NEW_DELETE

#ifndef SDL_WIIU_DONT_REPLACE_NEW_DELETE
// Override global new/delete operators to use SDL_malloc()/SDL_free()
void*
operator new(std::size_t size)
{
    if (void* data = SDL_malloc(size))
        return data;
    throw std::bad_alloc{};
}

void
operator delete(void* data)
    noexcept
{
    SDL_free(data);
}
#endif // SDL_WIIU_DONT_REPLACE_NEW_DELETE

namespace {
    namespace detail {

        struct FSLib {
            FSLib()
            {
                FSInit();
            }

            ~FSLib()
            {
                FSShutdown();
            }

        };

        std::optional<FSLib> fsLib;

        struct FSClientWrapper : FSClient {

            FSClientWrapper()
            {
                auto status = FSAddClient(this, FS_ERROR_FLAG_ALL);
                if (status != FS_STATUS_OK)
                    throw std::runtime_error{"FSAddClient() failed"};
            }

            // disallow moving
            FSClientWrapper(FSClientWrapper&&) = delete;

            ~FSClientWrapper()
            {
                FSDelClient(this, FS_ERROR_FLAG_NONE);
            }

        };


        bool initialized = false;

        std::unique_ptr<char[]> workMemory;
        std::unique_ptr<FSClientWrapper> fsClient;

        SDL_Window *currentWindow = nullptr;

        nn::swkbd::CreateArg createArg;
        const nn::swkbd::CreateArg *customCreateArg = nullptr;
        std::string createdLocale;

        namespace appear {
            const nn::swkbd::AppearArg *customArg = nullptr;

            // keyboard config options
            nn::swkbd::KeyboardMode keyboardMode = nn::swkbd::KeyboardMode::Full;
            std::u16string okText;
            bool showWordSuggestions = false;

            // TODO: control disabled inputs, needs to fix nn::swkbd::ConfigArg

            // input form options
            std::u16string initialText;
            std::u16string hintText;
            nn::swkbd::PasswordMode passwordMode = nn::swkbd::PasswordMode::Clear;
            bool highlightInitialText = false;
            bool showCopyPasteButtons = false;
        }

        nn::swkbd::ControllerInfo controllerInfo;

        VPADStatus vpad;
        std::array<KPADStatus, 4> kpad;

        SDL_SysWMmsg wmMsg;


        std::pair<std::string, std::string>
        parse_lang_str(const char* lang)
        {
            char language[3];
            char country[3];
            int r = SDL_sscanf(lang, "%2[Ca-z]_%2[A-Z]", language, country);
            if (r == 2)
                return { language, country };
            if (r == 1)
                return { language, {} };
            return {};
        }

        std::optional<LanguageType>
        to_language(const std::string& language, const std::string& country)
        {
            // two-letter codes, from ISO 639
            if (language == "ja")
                return LanguageType::Japanese;
            if (language == "en")
                return LanguageType::English;
            if (language == "fr")
                return LanguageType::French;
            if (language == "de")
                return LanguageType::German;
            if (language == "it")
                return LanguageType::Italian;
            if (language == "es")
                return LanguageType::Spanish;
            if (language == "zh") {
                if (country == "TW")
                    return LanguageType::TraditionalChinese;
                if (country == "CN")
                    return LanguageType::SimplifiedChinese;
                return LanguageType::TraditionalChinese;
            }
            if (language == "ko")
                return LanguageType::Korean;
            if (language == "nl")
                return LanguageType::Dutch;
            if (language == "pt")
                return LanguageType::Portuguese;
            if (language == "ru")
                return LanguageType::Russian;

            // printf("failed to detect language for [%s], [%s]\n",
            //        language.data(),
            //        country.data());
            return {};
        }

        std::optional<RegionType>
        to_region(const std::string& language, const std::string& country)
        {
            static const std::array usa_countries = {
                "US", "CA", "MX", "BR",
            };
            static const std::array eur_countries = {
                "DE", "ES", "FR", "GB", "IT", "NL", "PT", "RU",
            };

            static const std::array eur_languages = {
                "de", "es", "fr", "it", "nl", "pt", "ru",
            };

            if (country == "JP")
                return RegionType::Japan;

            for (auto c : usa_countries)
                if (country == c)
                    return RegionType::USA;

            for (auto c : eur_countries)
                if (country == c)
                    return RegionType::Europe;

            if (country == "CN")
                return RegionType::China;

            if (country == "KR" || country == "KP")
                return RegionType::Korea;

            if (country == "TW")
                return RegionType::Taiwan;

            // If country doesn't match, return a compatible region based on language alone
            if (language == "ja")
                return RegionType::Japan;
            if (language == "en")
                return RegionType::USA;
            for (auto lang : eur_languages)
                if (language == lang)
                    return RegionType::Europe;
            if (language == "zh")
                return RegionType::China;
            if (language == "ko")
                return RegionType::Korea;

            // printf("failed to detect region for [%s], [%s]\n",
            //        language.data(),
            //        country.data());
            return {};
        }

        uint32_t
        to_keyboard_layout(LanguageType language,
                           RegionType region)
        {
            switch (language) {
                case LanguageType::Japanese:
                    return 0;

                case LanguageType::English:
                    if (region == RegionType::USA)
                        return 1;
                    else
                        return 5;

                case LanguageType::French:
                    if (region == RegionType::USA)
                        return  2;
                    else
                        return 6;

                case LanguageType::German:
                    return 7;

                case LanguageType::Italian:
                    return 8;

                case LanguageType::Spanish:
                    if (region == RegionType::USA)
                        return 3;
                    else
                        return 9;

                case LanguageType::SimplifiedChinese:
                case LanguageType::TraditionalChinese:
                case LanguageType::Korean:
                    return 19;

                case LanguageType::Dutch:
                    return 10;

                case LanguageType::Portuguese:
                    if (region == RegionType::USA)
                        return 4;
                    else
                        return 11;

                case LanguageType::Russian:
                    return 12;

                default:
                    return 19;
            }
        }

        std::optional<LanguageType>
        get_language_from_locale()
        {
            const char* lang = std::setlocale(LC_CTYPE, nullptr);
            if (!lang)
                return {};
            if (lang[0] == '\0') // quickly handle empty string
                return {};
            if (lang == "C"s)
                return {};
            auto [language, country] = parse_lang_str(lang);
            return to_language(language, country);
        }

        std::optional<RegionType>
        get_region_from_locale()
        {
            const char* lang = std::setlocale(LC_CTYPE, nullptr);
            if (!lang)
                return {};
            if (lang[0] == '\0') // quickly handle empty string
                return {};
            if (lang == "C"s)
                return {};
            auto [language, country] = parse_lang_str(lang);
            return to_region(language, country);
        }

        uint32_t
        read_system_config_u32(const char* key)
            noexcept
        {
            UCHandle handle = UCOpen();
            if (handle < 0) {
                unsigned result;
                alignas(64) UCSysConfig arg{};
                SDL_strlcpy(arg.name, key, sizeof arg.name);
                arg.dataType = UC_DATATYPE_UNSIGNED_INT;
                arg.dataSize = sizeof result;
                arg.data = &result;
                auto status = UCReadSysConfig(handle, 1, &arg);
                UCClose(handle);
                if (status == UC_ERROR_OK)
                    return result;
            }
            // DEBUG
            // printf("failed to read config %s\n", key);
            return -1;
        }

        LanguageType
        read_system_language()
            noexcept
        {
            auto lang = read_system_config_u32("cafe.language");
            if (lang <= 11)
                return static_cast<LanguageType>(lang);
            return LanguageType::English;
        }

        LanguageType
        get_language_from_sys()
            noexcept
        {
            static LanguageType cached = read_system_language();
            return cached;
        }

        RegionType
        read_system_region()
            noexcept
        {
            alignas(64) MCPSysProdSettings settings{};
            MCPError status = 0;
            int handle = MCP_Open();
            if (handle < 0)
                goto error;
            status = MCP_GetSysProdSettings(handle, &settings);
            MCP_Close(handle);
            if (status)
                goto error;

            if (settings.product_area & MCP_REGION_JAPAN)
                return RegionType::Japan;
            if (settings.product_area & MCP_REGION_USA)
                return RegionType::USA;
            if (settings.product_area & MCP_REGION_EUROPE)
                return RegionType::Europe;
            if (settings.product_area & MCP_REGION_CHINA)
                return RegionType::China;
            if (settings.product_area & MCP_REGION_KOREA)
                return RegionType::Korea;
            if (settings.product_area & MCP_REGION_TAIWAN)
                return RegionType::Taiwan;

        error:
            return RegionType::Europe;
        }


        RegionType
        get_region_from_sys()
            noexcept
        {
            static RegionType cached = read_system_region();
            return cached;
        }

        std::size_t
        strlen_16(const char16_t* s)
        {
            if (!s)
                return 0;
            std::size_t result = 0;
            while (*s++)
                ++result;
            return result;
        }

        std::u8string
        to_utf8(const char16_t* input)
        {
            auto output = SDL_iconv_string("UTF-8",
                                           "UTF-16BE",
                                           reinterpret_cast<const char*>(input),
                                           2 * (strlen_16(input) + 1));
            if (!output)
                throw std::runtime_error{"SDL_iconv_string() failed"};

            try {
                std::u8string result(reinterpret_cast<char8_t*>(output));
                SDL_free(output);
                return result;
            }
            catch (...) {
                SDL_free(output);
                throw;
            }
        }

        std::u16string
        to_utf16(const char* input)
        {
            auto output = SDL_iconv_string("UTF-16BE",
                                           "UTF-8",
                                           input,
                                           SDL_strlen(input) + 1);
            if (!output)
                throw std::runtime_error{"SDL_iconv_string() failed"};
            try {
                std::u16string result(reinterpret_cast<char16_t*>(output));
                SDL_free(output);
                return result;
            }
            catch (...) {
                SDL_free(output);
                throw;
            }
        }

    } // namespace detail
} // namespace

void WIIU_SWKBD_Initialize(void)
{
    if (detail::initialized)
        return;

    try {
        if (!detail::fsLib)
            detail::fsLib.emplace();

        if (detail::customCreateArg)
            detail::createArg = *detail::customCreateArg;
        else {
            detail::createArg = {};
            if (auto loc_region = detail::get_region_from_locale())
                detail::createArg.regionType = *loc_region;
            else
                detail::createArg.regionType = detail::get_region_from_sys();
        }

        std::unique_ptr<detail::FSClientWrapper> local_fsClient;
        if (!detail::createArg.fsClient) {
            local_fsClient = std::make_unique<detail::FSClientWrapper>();
            detail::createArg.fsClient = local_fsClient.get();
        }

        std::unique_ptr<char[]> local_workMemory;
        if (!detail::createArg.workMemory) {
            local_workMemory = std::make_unique<char[]>(nn::swkbd::GetWorkMemorySize(0));
            detail::createArg.workMemory = local_workMemory.get();
        }

        if (!nn::swkbd::Create(detail::createArg))
            return;

        detail::workMemory = std::move(local_workMemory);
        detail::fsClient   = std::move(local_fsClient);

        if (const char* loc = std::setlocale(LC_CTYPE, nullptr))
            detail::createdLocale = loc;
        else
            detail::createdLocale.clear();

        detail::initialized = true;
    }
    catch (...) {
    }
}

__attribute__ (( __destructor__ ))
void WIIU_SWKBD_Finalize(void)
{
    if (!detail::initialized)
        return;

    nn::swkbd::Destroy();
    detail::workMemory.reset();
    detail::fsClient.reset();
    detail::currentWindow = nullptr;
    detail::createdLocale.clear();
    detail::initialized = false;
}

SDL_bool WIIU_SWKBD_ConsumeVPAD(const VPADStatus *vpad)
{
    if (!detail::initialized)
        return SDL_FALSE;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state != nn::swkbd::State::Visible)
        return SDL_FALSE;

    // printf("swkbd is consuming vpad input\n");
    detail::vpad = *vpad;
    detail::controllerInfo.vpad = &detail::vpad;
    return SDL_TRUE;
}

SDL_bool WIIU_SWKBD_ConsumeKPAD(KPADChan channel, const KPADStatus *kpad)
{
    if (!detail::initialized)
        return SDL_FALSE;

    if (channel < 0 || channel > 3)
        return SDL_FALSE;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state != nn::swkbd::State::Visible)
        return SDL_FALSE;

    // printf("swkbd is consuming kpad input\n");
    detail::kpad[channel] = *kpad;
    detail::controllerInfo.kpad[channel] = &detail::kpad[channel];
    return SDL_TRUE;
}

void WIIU_SWKBD_Calc(void)
{
    if (!detail::initialized)
        return;

    nn::swkbd::Calc(detail::controllerInfo);
    detail::controllerInfo = {};

    // TODO: these could go into a background thread
    if (nn::swkbd::IsNeedCalcSubThreadFont())
        nn::swkbd::CalcSubThreadFont();
    if (nn::swkbd::IsNeedCalcSubThreadPredict())
        nn::swkbd::CalcSubThreadPredict();

    if (detail::currentWindow) {
        nn::swkbd::State state = nn::swkbd::GetStateInputForm();
        if (state == nn::swkbd::State::Hidden)
            detail::currentWindow = nullptr;
    }

    // Check if user confirmed input.
    if (nn::swkbd::IsDecideOkButton(nullptr)) {
        try {
            auto str16 = nn::swkbd::GetInputFormString();
            if (str16) {
                auto str8 = detail::to_utf8(str16);
                SDL_SendKeyboardText(reinterpret_cast<const char*>(str8.data()));
            }
        }
        catch (std::exception& e) {
            std::printf("could not convert utf-16 to utf-8: %s\n", e.what());
        }

        WIIU_SWKBD_HideScreenKeyboard(nullptr, nullptr);

        // notify application
        SDL_VERSION(&detail::wmMsg.version);
        detail::wmMsg.subsystem = SDL_SYSWM_WIIU;
        detail::wmMsg.msg.wiiu.event = SDL_WIIU_SYSWM_SWKBD_OK_EVENT;
        SDL_SendSysWMEvent(&detail::wmMsg);
    }

    if (nn::swkbd::IsDecideCancelButton(nullptr)) {
        WIIU_SWKBD_HideScreenKeyboard(nullptr, nullptr);

        // notify application
        SDL_VERSION(&detail::wmMsg.version);
        detail::wmMsg.subsystem = SDL_SYSWM_WIIU;
        detail::wmMsg.msg.wiiu.event = SDL_WIIU_SYSWM_SWKBD_CANCEL_EVENT;
        SDL_SendSysWMEvent(&detail::wmMsg);
    }
}

void WIIU_SWKBD_Draw(SDL_Window *window)
{
    if (window != detail::currentWindow)
        return;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state == nn::swkbd::State::Hidden)
        return;

    if (window->flags & SDL_WINDOW_WIIU_TV_ONLY)
        nn::swkbd::DrawTV();
    else
        nn::swkbd::DrawDRC();
}

SDL_bool WIIU_SWKBD_HasScreenKeyboardSupport(_THIS)
{
    return SDL_TRUE;
}

void WIIU_SWKBD_ShowScreenKeyboard(_THIS, SDL_Window *window)
{
    // If the locale changed, force it to be constructed again.
    if (const char* loc = std::setlocale(LC_CTYPE, nullptr)) {
        if (loc != detail::createdLocale)
            WIIU_SWKBD_Finalize();
    }

    WIIU_SWKBD_Initialize();

    if (!detail::currentWindow)
        detail::currentWindow = window;

    nn::swkbd::AppearArg arg;
    if (detail::appear::customArg)
        arg = *detail::appear::customArg;
    else {
        // Set language
        if (auto loc_lang = detail::get_language_from_locale())
            arg.keyboardArg.configArg.languageType = *loc_lang;
        else
            arg.keyboardArg.configArg.languageType = detail::get_language_from_sys();

        // set keyboard layout according to language
        // TODO: fix wut: it's the unk_0x10 field
        arg.keyboardArg.configArg.unk_0x10
            = detail::to_keyboard_layout(arg.keyboardArg.configArg.languageType,
                                         detail::createArg.regionType);

        // Set keyboard mode
        arg.keyboardArg.configArg.keyboardMode = detail::appear::keyboardMode;

        // Set OK text
        if (!detail::appear::okText.empty())
            arg.keyboardArg.configArg.okString = detail::appear::okText.data();

        // Set word suggestions
        arg.keyboardArg.configArg.showWordSuggestions = detail::appear::showWordSuggestions;

        // Always enable Wiimote pointer
        arg.keyboardArg.configArg.drawSysWiiPointer = true;

        // Set initial text
        if (!detail::appear::initialText.empty())
            arg.inputFormArg.initialText = detail::appear::initialText.data();

        // Set hint text
        if (!detail::appear::hintText.empty())
            arg.inputFormArg.hintText = detail::appear::hintText.data();

        // Set password mode
        arg.inputFormArg.passwordMode = detail::appear::passwordMode;

        // Set highlight initial text
        // Note the typo, must fix this after we fix WUT
        arg.inputFormArg.higlightInitialText = detail::appear::highlightInitialText;

        // Set show copy paste buttons
        arg.inputFormArg.showCopyPasteButtons = detail::appear::showCopyPasteButtons;

    }

    if (window->flags & SDL_WINDOW_WIIU_TV_ONLY)
        arg.keyboardArg.configArg.controllerType = nn::swkbd::ControllerType::WiiRemote0;
    else
        arg.keyboardArg.configArg.controllerType = nn::swkbd::ControllerType::DrcGamepad;

    nn::swkbd::AppearInputForm(arg);

    // Reset all customization
    detail::appear::keyboardMode = nn::swkbd::KeyboardMode::Full;
    detail::appear::okText.clear();
    detail::appear::showWordSuggestions = false;
    detail::appear::initialText.clear();
    detail::appear::hintText.clear();
    detail::appear::passwordMode = nn::swkbd::PasswordMode::Clear;
    detail::appear::highlightInitialText = false;
    detail::appear::showCopyPasteButtons = false;
}

void WIIU_SWKBD_HideScreenKeyboard(_THIS, SDL_Window *)
{
    if (!detail::initialized)
        return;

    // printf("hiding swkbd\n");
    nn::swkbd::DisappearInputForm();
    detail::currentWindow = nullptr;
}

SDL_bool WIIU_SWKBD_IsScreenKeyboardShown(_THIS, SDL_Window *)
{
    if (!detail::initialized)
        return SDL_FALSE;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state != nn::swkbd::State::Hidden)
        return SDL_TRUE;

    return SDL_FALSE;
}

void SDL_WiiUSetSWKBDCreateArg(void * arg)
{
    detail::customCreateArg = reinterpret_cast<const nn::swkbd::CreateArg*>(arg);
    // force swkbd to be created again next time it's shown
    WIIU_SWKBD_Finalize();
}

void SDL_WiiUSetSWKBDAppearArg(void * arg)
{
    detail::appear::customArg = reinterpret_cast<const nn::swkbd::AppearArg*>(arg);
}

void SDL_WiiUSetSWKBDKeyboardMode(int mode)
{
    switch (mode) {
        case 1: // numpad
            detail::appear::keyboardMode = nn::swkbd::KeyboardMode::Numpad;
            break;
        case 2: // restricted
            detail::appear::keyboardMode = nn::swkbd::KeyboardMode::Utf8;
            break;
        case 3: // NNID
            detail::appear::keyboardMode = nn::swkbd::KeyboardMode::NNID;
            break;
        default:
        case 0: // full
            detail::appear::keyboardMode = nn::swkbd::KeyboardMode::Full;
    }
}

void SDL_WiiUSetSWKBDOKLabel(const char * label)
{
    if (label)
        detail::appear::okText = detail::to_utf16(label);
    else
        detail::appear::okText.clear();
}

void SDL_WiiUSetSWKBDShowWordSuggestions(SDL_bool show)
{
    detail::appear::showWordSuggestions = show;
}

void SDL_WiiUSetSWKBDInitialText(const char * text)
{
    if (text)
        detail::appear::initialText = detail::to_utf16(text);
    else
        detail::appear::initialText.clear();
}

void SDL_WiiUSetSWKBDHintText(const char * text)
{
    if (text)
        detail::appear::hintText = detail::to_utf16(text);
    else
        detail::appear::hintText.clear();
}

void SDL_WiiUSetSWKBDPasswordMode(int mode)
{
    switch (mode) {
        case 1:
            detail::appear::passwordMode = nn::swkbd::PasswordMode::Hide;
            break;
        case 2:
            detail::appear::passwordMode = nn::swkbd::PasswordMode::Fade;
            break;
        case 0:
        default:
            detail::appear::passwordMode = nn::swkbd::PasswordMode::Clear;
    }
}

void SDL_WiiUSetSWKBDHighlightInitialText(SDL_bool highlight)
{
    detail::appear::highlightInitialText = highlight;
}

void SDL_WiiUSetSWKBDShowCopyPasteButtons(SDL_bool show)
{
    detail::appear::showCopyPasteButtons = show;
}
