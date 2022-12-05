/*
	Copyright 2019 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <mutex>
#include "gui.h"
#include "osd.h"
#include "cfg/cfg.h"
#include "hw/maple/maple_if.h"
#include "hw/maple/maple_devs.h"
#include "hw/naomi/naomi_cart.h"
#include "imgui/imgui.h"
#include "imgui/roboto_medium.h"
#include "network/net_handshake.h"
#include "network/ggpo.h"
#include "wsi/context.h"
#include "input/gamepad_device.h"
#include "input/mouse.h"
#include "gui_util.h"
#include "gui_android.h"
#include "game_scanner.h"
#include "version.h"
#include "oslib/oslib.h"
#include "oslib/audiostream.h"
#include "imgread/common.h"
#include "log/LogManager.h"
#include "emulator.h"
#include "rend/mainui.h"
#include "lua/lua.h"
#include "gui_chat.h"
#include "imgui_driver.h"

static bool game_started;

int insetLeft, insetRight, insetTop, insetBottom;
std::unique_ptr<ImGuiDriver> imguiDriver;

static bool inited = false;
GuiState gui_state = GuiState::Main;
static bool commandLineStart;
static u32 mouseButtons;
static int mouseX, mouseY;
static float mouseWheel;
static std::string error_msg;
static bool error_msg_shown;
static std::string osd_message;
static double osd_message_end;
static std::mutex osd_message_mutex;
static void (*showOnScreenKeyboard)(bool show);
static bool keysUpNextFrame[512];

static int map_system = 0;
static void reset_vmus();
void error_popup();

static GameScanner scanner;
static BackgroundGameLoader gameLoader;
static Chat chat;

static void emuEventCallback(Event event, void *)
{
	switch (event)
	{
	case Event::Resume:
		game_started = true;
		break;
	case Event::Start:
	case Event::Terminate:
		GamepadDevice::load_system_mappings();
		break;
	default:
		break;
	}
}

void gui_init()
{
	if (inited)
		return;
	inited = true;

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	io.IniFilename = NULL;

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

	io.KeyMap[ImGuiKey_Tab] = 0x2B;
	io.KeyMap[ImGuiKey_LeftArrow] = 0x50;
	io.KeyMap[ImGuiKey_RightArrow] = 0x4F;
	io.KeyMap[ImGuiKey_UpArrow] = 0x52;
	io.KeyMap[ImGuiKey_DownArrow] = 0x51;
	io.KeyMap[ImGuiKey_PageUp] = 0x4B;
	io.KeyMap[ImGuiKey_PageDown] = 0x4E;
	io.KeyMap[ImGuiKey_Home] = 0x4A;
	io.KeyMap[ImGuiKey_End] = 0x4D;
	io.KeyMap[ImGuiKey_Insert] = 0x49;
	io.KeyMap[ImGuiKey_Delete] = 0x4C;
	io.KeyMap[ImGuiKey_Backspace] = 0x2A;
	io.KeyMap[ImGuiKey_Space] = 0x2C;
	io.KeyMap[ImGuiKey_Enter] = 0x28;
	io.KeyMap[ImGuiKey_Escape] = 0x29;
	io.KeyMap[ImGuiKey_A] = 0x04;
	io.KeyMap[ImGuiKey_C] = 0x06;
	io.KeyMap[ImGuiKey_V] = 0x19;
	io.KeyMap[ImGuiKey_X] = 0x1B;
	io.KeyMap[ImGuiKey_Y] = 0x1C;
	io.KeyMap[ImGuiKey_Z] = 0x1D;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();
    ImGui::GetStyle().TabRounding = 0;
    ImGui::GetStyle().ItemSpacing = ImVec2(8, 8);		// from 8,4
    ImGui::GetStyle().ItemInnerSpacing = ImVec2(4, 6);	// from 4,4
    //ImGui::GetStyle().WindowRounding = 0;
#if defined(__ANDROID__) || defined(TARGET_IPHONE)
    ImGui::GetStyle().TouchExtraPadding = ImVec2(1, 1);	// from 0,0
#endif
    EventManager::listen(Event::Resume, emuEventCallback);
    EventManager::listen(Event::Start, emuEventCallback);
	EventManager::listen(Event::Terminate, emuEventCallback);
    ggpo::receiveChatMessages([](int playerNum, const std::string& msg) { chat.receive(playerNum, msg); });
}

void gui_initFonts()
{
	static float uiScale;

	verify(inited);
	if (settings.display.uiScale == uiScale && ImGui::GetIO().Fonts->IsBuilt())
		return;

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'misc/fonts/README.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);
#if !defined(TARGET_UWP) && !defined(__SWITCH__)
	settings.display.uiScale = std::max(1.f, settings.display.dpi / 100.f * 0.75f);
   	// Limit scaling on small low-res screens
    if (settings.display.width <= 640 || settings.display.height <= 480)
    	settings.display.uiScale = std::min(1.4f, settings.display.uiScale);
#endif
    uiScale = settings.display.uiScale;
    if (settings.display.uiScale > 1)
    	ImGui::GetStyle().ScaleAllSizes(settings.display.uiScale);

    static const ImWchar ranges[] =
    {
    	0x0020, 0xFFFF, // All chars
        0,
    };

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
	const float fontSize = 17.f * settings.display.uiScale;
	io.Fonts->AddFontFromFileTTF("/storage/.config/retroarch/regular.ttf", fontSize, nullptr, ranges);
	//io.Fonts->AddFontFromMemoryCompressedTTF(roboto_medium_compressed_data, roboto_medium_compressed_size, fontSize, nullptr, ranges);
    ImFontConfig font_cfg;
    font_cfg.MergeMode = true;

#ifdef _WIN32
    u32 cp = GetACP();
    std::string fontDir = std::string(nowide::getenv("SYSTEMROOT")) + "\\Fonts\\";
    switch (cp)
    {
    case 932:	// Japanese
		{
			font_cfg.FontNo = 2;	// UIGothic
			ImFont* font = io.Fonts->AddFontFromFileTTF((fontDir + "msgothic.ttc").c_str(), fontSize, &font_cfg, io.Fonts->GetGlyphRangesJapanese());
			font_cfg.FontNo = 2;	// Meiryo UI
			if (font == nullptr)
				io.Fonts->AddFontFromFileTTF((fontDir + "Meiryo.ttc").c_str(), fontSize, &font_cfg, io.Fonts->GetGlyphRangesJapanese());
		}
		break;
    case 949:	// Korean
		{
			ImFont* font = io.Fonts->AddFontFromFileTTF((fontDir + "Malgun.ttf").c_str(), fontSize, &font_cfg, io.Fonts->GetGlyphRangesKorean());
			if (font == nullptr)
			{
				font_cfg.FontNo = 2;	// Dotum
				io.Fonts->AddFontFromFileTTF((fontDir + "Gulim.ttc").c_str(), fontSize, &font_cfg, io.Fonts->GetGlyphRangesKorean());
			}
		}
    	break;
    case 950:	// Traditional Chinese
		{
			font_cfg.FontNo = 1; // Microsoft JhengHei UI Regular
			ImFont* font = io.Fonts->AddFontFromFileTTF((fontDir + "Msjh.ttc").c_str(), fontSize, &font_cfg, GetGlyphRangesChineseTraditionalOfficial());
			font_cfg.FontNo = 0;
			if (font == nullptr)
				io.Fonts->AddFontFromFileTTF((fontDir + "MSJH.ttf").c_str(), fontSize, &font_cfg, GetGlyphRangesChineseTraditionalOfficial());
		}
    	break;
    case 936:	// Simplified Chinese
		io.Fonts->AddFontFromFileTTF((fontDir + "Simsun.ttc").c_str(), fontSize, &font_cfg, GetGlyphRangesChineseSimplifiedOfficial());
    	break;
    default:
    	break;
    }
#elif defined(__APPLE__) && !defined(TARGET_IPHONE)
    std::string fontDir = std::string("/System/Library/Fonts/");

    extern std::string os_Locale();
    std::string locale = os_Locale();

    if (locale.find("ja") == 0)             // Japanese
    {
        io.Fonts->AddFontFromFileTTF((fontDir + "ヒラギノ角ゴシック W4.ttc").c_str(), fontSize, &font_cfg, io.Fonts->GetGlyphRangesJapanese());
    }
    else if (locale.find("ko") == 0)       // Korean
    {
        io.Fonts->AddFontFromFileTTF((fontDir + "AppleSDGothicNeo.ttc").c_str(), fontSize, &font_cfg, io.Fonts->GetGlyphRangesKorean());
    }
    else if (locale.find("zh-Hant") == 0)  // Traditional Chinese
    {
        io.Fonts->AddFontFromFileTTF((fontDir + "PingFang.ttc").c_str(), fontSize, &font_cfg, GetGlyphRangesChineseTraditionalOfficial());
    }
    else if (locale.find("zh-Hans") == 0)  // Simplified Chinese
    {
        io.Fonts->AddFontFromFileTTF((fontDir + "PingFang.ttc").c_str(), fontSize, &font_cfg, GetGlyphRangesChineseSimplifiedOfficial());
    }
#elif defined(__ANDROID__)
    if (getenv("FLYCAST_LOCALE") != nullptr)
    {
    	const ImWchar *glyphRanges = nullptr;
    	std::string locale = getenv("FLYCAST_LOCALE");
        if (locale.find("ja") == 0)				// Japanese
        	glyphRanges = io.Fonts->GetGlyphRangesJapanese();
        else if (locale.find("ko") == 0)		// Korean
        	glyphRanges = io.Fonts->GetGlyphRangesKorean();
        else if (locale.find("zh_TW") == 0
        		|| locale.find("zh_HK") == 0)	// Traditional Chinese
        	glyphRanges = GetGlyphRangesChineseTraditionalOfficial();
        else if (locale.find("zh_CN") == 0)		// Simplified Chinese
        	glyphRanges = GetGlyphRangesChineseSimplifiedOfficial();

        if (glyphRanges != nullptr)
        	io.Fonts->AddFontFromFileTTF("/system/fonts/NotoSansCJK-Regular.ttc", fontSize, &font_cfg, glyphRanges);
    }

    // TODO Linux, iOS, ...
#endif
	NOTICE_LOG(RENDERER, "Screen DPI is %.0f, size %d x %d. Scaling by %.2f", settings.display.dpi, settings.display.width, settings.display.height, settings.display.uiScale);
}

void gui_keyboard_input(u16 wc)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureKeyboard)
		io.AddInputCharacter(wc);
}

void gui_keyboard_inputUTF8(const std::string& s)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureKeyboard)
		io.AddInputCharactersUTF8(s.c_str());
}

void gui_keyboard_key(u8 keyCode, bool pressed, u8 modifiers)
{
	if (!inited)
		return;
	ImGuiIO& io = ImGui::GetIO();
	if (!pressed && io.KeysDown[keyCode])
	{
		keysUpNextFrame[keyCode] = true;
		return;
	}
	io.KeyCtrl = (modifiers & (0x01 | 0x10)) != 0;
	io.KeyShift = (modifiers & (0x02 | 0x20)) != 0;
	io.KeysDown[keyCode] = pressed;
}

bool gui_keyboard_captured()
{
	ImGuiIO& io = ImGui::GetIO();
	return io.WantCaptureKeyboard;
}

bool gui_mouse_captured()
{
	ImGuiIO& io = ImGui::GetIO();
	return io.WantCaptureMouse;
}

void gui_set_mouse_position(int x, int y)
{
	mouseX = std::round(x * settings.display.pointScale);
	mouseY = std::round(y * settings.display.pointScale);
}

void gui_set_mouse_button(int button, bool pressed)
{
	if (pressed)
		mouseButtons |= 1 << button;
	else
		mouseButtons &= ~(1 << button);
}

void gui_set_mouse_wheel(float delta)
{
	mouseWheel += delta;
}

static void gui_newFrame()
{
	imguiDriver->newFrame();
	ImGui::GetIO().DisplaySize.x = settings.display.width;
	ImGui::GetIO().DisplaySize.y = settings.display.height;

	ImGuiIO& io = ImGui::GetIO();

	if (mouseX < 0 || mouseX >= settings.display.width || mouseY < 0 || mouseY >= settings.display.height)
		io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
	else
		io.MousePos = ImVec2(mouseX, mouseY);
	static bool delayTouch;
#if defined(__ANDROID__) || defined(TARGET_IPHONE)
	// Delay touch by one frame to allow widgets to be hovered before click
	// This is required for widgets using ImGuiButtonFlags_AllowItemOverlap such as TabItem's
	if (!delayTouch && (mouseButtons & (1 << 0)) != 0 && !io.MouseDown[ImGuiMouseButton_Left])
		delayTouch = true;
	else
		delayTouch = false;
#endif
	if (io.WantCaptureMouse)
	{
		io.MouseWheel = -mouseWheel / 16;
		mouseWheel = 0;
	}
	if (!delayTouch)
		io.MouseDown[ImGuiMouseButton_Left] = (mouseButtons & (1 << 0)) != 0;
	io.MouseDown[ImGuiMouseButton_Right] = (mouseButtons & (1 << 1)) != 0;
	io.MouseDown[ImGuiMouseButton_Middle] = (mouseButtons & (1 << 2)) != 0;
	io.MouseDown[3] = (mouseButtons & (1 << 3)) != 0;

	io.NavInputs[ImGuiNavInput_Activate] = (kcode[0] & DC_BTN_A) == 0;
	io.NavInputs[ImGuiNavInput_Cancel] = (kcode[0] & DC_BTN_B) == 0;
	io.NavInputs[ImGuiNavInput_Input] = (kcode[0] & DC_BTN_X) == 0;
	io.NavInputs[ImGuiNavInput_DpadLeft] = (kcode[0] & DC_DPAD_LEFT) == 0;
	io.NavInputs[ImGuiNavInput_DpadRight] = (kcode[0] & DC_DPAD_RIGHT) == 0;
	io.NavInputs[ImGuiNavInput_DpadUp] = (kcode[0] & DC_DPAD_UP) == 0;
	io.NavInputs[ImGuiNavInput_DpadDown] = (kcode[0] & DC_DPAD_DOWN) == 0;
	io.NavInputs[ImGuiNavInput_LStickLeft] = joyx[0] < 0 ? -(float)joyx[0] / 128 : 0.f;
	if (io.NavInputs[ImGuiNavInput_LStickLeft] < 0.1f)
		io.NavInputs[ImGuiNavInput_LStickLeft] = 0.f;
	io.NavInputs[ImGuiNavInput_LStickRight] = joyx[0] > 0 ? (float)joyx[0] / 128 : 0.f;
	if (io.NavInputs[ImGuiNavInput_LStickRight] < 0.1f)
		io.NavInputs[ImGuiNavInput_LStickRight] = 0.f;
	io.NavInputs[ImGuiNavInput_LStickUp] = joyy[0] < 0 ? -(float)joyy[0] / 128.f : 0.f;
	if (io.NavInputs[ImGuiNavInput_LStickUp] < 0.1f)
		io.NavInputs[ImGuiNavInput_LStickUp] = 0.f;
	io.NavInputs[ImGuiNavInput_LStickDown] = joyy[0] > 0 ? (float)joyy[0] / 128.f : 0.f;
	if (io.NavInputs[ImGuiNavInput_LStickDown] < 0.1f)
		io.NavInputs[ImGuiNavInput_LStickDown] = 0.f;

	ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);

	if (showOnScreenKeyboard != nullptr)
		showOnScreenKeyboard(io.WantTextInput);
}

static void delayedKeysUp()
{
	ImGuiIO& io = ImGui::GetIO();
	for (u32 i = 0; i < ARRAY_SIZE(keysUpNextFrame); i++)
		if (keysUpNextFrame[i])
			io.KeysDown[i] = false;
	memset(keysUpNextFrame, 0, sizeof(keysUpNextFrame));
}

static void gui_endFrame()
{
    ImGui::Render();
    imguiDriver->renderDrawData(ImGui::GetDrawData());
    delayedKeysUp();
}

void gui_setOnScreenKeyboardCallback(void (*callback)(bool show))
{
	showOnScreenKeyboard = callback;
}

void gui_set_insets(int left, int right, int top, int bottom)
{
	insetLeft = left;
	insetRight = right;
	insetTop = top;
	insetBottom = bottom;
}

#if 0
#include "oslib/timeseries.h"
TimeSeries renderTimes;
TimeSeries vblankTimes;

void gui_plot_render_time(int width, int height)
{
	std::vector<float> v = renderTimes.data();
	ImGui::PlotLines("Render Times", v.data(), v.size(), 0, "", 0.0, 1.0 / 30.0, ImVec2(300, 50));
	ImGui::Text("StdDev: %.1f%%", renderTimes.stddev() * 100.f / 0.01666666667f);
	v = vblankTimes.data();
	ImGui::PlotLines("VBlank", v.data(), v.size(), 0, "", 0.0, 1.0 / 30.0, ImVec2(300, 50));
	ImGui::Text("StdDev: %.1f%%", vblankTimes.stddev() * 100.f / 0.01666666667f);
}
#endif

void gui_open_settings()
{
	if (gui_state == GuiState::Closed)
	{
		if (!ggpo::active())
		{
			gui_state = GuiState::Commands;
			HideOSD();
			emu.stop();
		}
		else
			chat.toggle();
	}
	else if (gui_state == GuiState::VJoyEdit)
	{
		gui_state = GuiState::VJoyEditCommands;
	}
	else if (gui_state == GuiState::Loading)
	{
		gameLoader.cancel();
	}
	else if (gui_state == GuiState::Commands)
	{
		gui_state = GuiState::Closed;
		GamepadDevice::load_system_mappings();
		emu.start();
	}
}

void gui_start_game(const std::string& path)
{
	emu.unloadGame();
	reset_vmus();
    chat.reset();

	scanner.stop();
	gui_state = GuiState::Loading;
	gameLoader.load(path);
}

void gui_stop_game(const std::string& message)
{
	if (!commandLineStart)
	{
		// Exit to main menu
		emu.unloadGame();
		gui_state = GuiState::Main;
		game_started = false;
		reset_vmus();
		if (!message.empty())
			gui_error("Flycast已停止.\n\n" + message);
	}
	else
	{
		if (!message.empty())
			ERROR_LOG(COMMON, "Flycast已停止: %s", message.c_str());
		// Exit emulator
		dc_exit();
	}
}

static void gui_display_commands()
{
   	imguiDriver->displayVmus();

    centerNextWindow();
    ImGui::SetNextWindowSize(ScaledVec2(330, 0));

    ImGui::Begin("##commands", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

    bool loadSaveStateDisabled = settings.content.path.empty() || settings.network.online;
	if (loadSaveStateDisabled)
	{
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}

	// Load State
	if (ImGui::Button("读取存档", ScaledVec2(110, 50)) && !loadSaveStateDisabled)
	{
		gui_state = GuiState::Closed;
		dc_loadstate(config::SavestateSlot);
	}
	ImGui::SameLine();

	// Slot #
	std::string slot = "档槽 " + std::to_string((int)config::SavestateSlot + 1);
	if (ImGui::Button(slot.c_str(), ImVec2(80 * settings.display.uiScale - ImGui::GetStyle().FramePadding.x, 50 * settings.display.uiScale)))
		ImGui::OpenPopup("slot_select_popup");
    if (ImGui::BeginPopup("slot_select_popup"))
    {
        for (int i = 0; i < 10; i++)
            if (ImGui::Selectable(std::to_string(i + 1).c_str(), config::SavestateSlot == i, 0,
            		ImVec2(ImGui::CalcTextSize("档槽 8").x, 0))) {
                config::SavestateSlot = i;
                SaveSettings();
            }
        ImGui::EndPopup();
    }
	ImGui::SameLine();

	// Save State
	if (ImGui::Button("保存存档", ScaledVec2(110, 50)) && !loadSaveStateDisabled)
	{
		gui_state = GuiState::Closed;
		dc_savestate(config::SavestateSlot);
	}
	if (loadSaveStateDisabled)
	{
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
	}

	ImGui::Columns(2, "buttons", false);

	// Settings
	if (ImGui::Button("设置", ScaledVec2(150, 50)))
	{
		gui_state = GuiState::Settings;
	}
	ImGui::NextColumn();
	if (ImGui::Button("重新开始", ScaledVec2(150, 50)))
	{
		GamepadDevice::load_system_mappings();
		gui_state = GuiState::Closed;
	}

	ImGui::NextColumn();

	// Insert/Eject Disk
	const char *disk_label = libGDR_GetDiscType() == Open ? "放入磁盘" : "弹出磁盘";
	if (ImGui::Button(disk_label, ScaledVec2(150, 50)))
	{
		if (libGDR_GetDiscType() == Open)
		{
			gui_state = GuiState::SelectDisk;
		}
		else
		{
			DiscOpenLid();
			gui_state = GuiState::Closed;
		}
	}
	ImGui::NextColumn();

	// Cheats
	if (settings.network.online)
	{
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}
	if (ImGui::Button("金手指", ScaledVec2(150, 50)) && !settings.network.online)
	{
		gui_state = GuiState::Cheats;
	}
	if (settings.network.online)
	{
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
	}
	ImGui::Columns(1, nullptr, false);

	// Exit
	if (ImGui::Button("退出", ScaledVec2(300, 50)
			+ ImVec2(ImGui::GetStyle().ColumnsMinSpacing + ImGui::GetStyle().FramePadding.x * 2 - 1, 0)))
	{
		gui_stop_game();
	}

	ImGui::End();
}

inline static void header(const char *title)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f)); // Left
	ImGui::ButtonEx(title, ImVec2(-1, 0), ImGuiButtonFlags_Disabled);
	ImGui::PopStyleVar();
}

const char *maple_device_types[] = { "没有", "世嘉控制器", "光枪", "键盘", "鼠标", "Twin Stick", "Ascii Stick" };
const char *maple_expansion_device_types[] = { "没有", "世嘉VMU", "Purupuru", "Microphone" };

static const char *maple_device_name(MapleDeviceType type)
{
	switch (type)
	{
	case MDT_SegaController:
		return maple_device_types[1];
	case MDT_LightGun:
		return maple_device_types[2];
	case MDT_Keyboard:
		return maple_device_types[3];
	case MDT_Mouse:
		return maple_device_types[4];
	case MDT_TwinStick:
		return maple_device_types[5];
	case MDT_AsciiStick:
		return maple_device_types[6];
	case MDT_None:
	default:
		return maple_device_types[0];
	}
}

static MapleDeviceType maple_device_type_from_index(int idx)
{
	switch (idx)
	{
	case 1:
		return MDT_SegaController;
	case 2:
		return MDT_LightGun;
	case 3:
		return MDT_Keyboard;
	case 4:
		return MDT_Mouse;
	case 5:
		return MDT_TwinStick;
	case 6:
		return MDT_AsciiStick;
	case 0:
	default:
		return MDT_None;
	}
}

static const char *maple_expansion_device_name(MapleDeviceType type)
{
	switch (type)
	{
	case MDT_SegaVMU:
		return maple_expansion_device_types[1];
	case MDT_PurupuruPack:
		return maple_expansion_device_types[2];
	case MDT_Microphone:
		return maple_expansion_device_types[3];
	case MDT_None:
	default:
		return maple_expansion_device_types[0];
	}
}

const char *maple_ports[] = { "没有", "A", "B", "C", "D", "全部" };

struct Mapping {
	DreamcastKey key;
	const char *name;
};

const Mapping dcButtons[] = {
	{ EMU_BTN_NONE, "方向" },
	{ DC_DPAD_UP, "上" },
	{ DC_DPAD_DOWN, "下" },
	{ DC_DPAD_LEFT, "左" },
	{ DC_DPAD_RIGHT, "右" },

	{ DC_AXIS_UP, "拇指摇杆 上" },
	{ DC_AXIS_DOWN, "拇指摇杆 下" },
	{ DC_AXIS_LEFT, "拇指摇杆 左" },
	{ DC_AXIS_RIGHT, "拇指摇杆 右" },

	{ DC_DPAD2_UP, "控制器2 上" },
	{ DC_DPAD2_DOWN, "控制器2 下" },
	{ DC_DPAD2_LEFT, "控制器2 左" },
	{ DC_DPAD2_RIGHT, "控制器2 右" },

	{ EMU_BTN_NONE, "按键" },
	{ DC_BTN_A, "A" },
	{ DC_BTN_B, "B" },
	{ DC_BTN_X, "X" },
	{ DC_BTN_Y, "Y" },
	{ DC_BTN_C, "C" },
	{ DC_BTN_D, "D" },
	{ DC_BTN_Z, "Z" },

	{ EMU_BTN_NONE, "触发器" },
	{ DC_AXIS_LT, "左 触发器" },
	{ DC_AXIS_RT, "右 触发器" },

	{ EMU_BTN_NONE, "系统按键" },
	{ DC_BTN_START, "开始" },
	{ DC_BTN_RELOAD, "重新加载" },

	{ EMU_BTN_NONE, "模拟器" },
	{ EMU_BTN_MENU, "菜单" },
	{ EMU_BTN_ESCAPE, "退出" },
	{ EMU_BTN_FFORWARD, "快进" },

	{ EMU_BTN_NONE, nullptr }
};

const Mapping arcadeButtons[] = {
	{ EMU_BTN_NONE, "方向" },
	{ DC_DPAD_UP, "上" },
	{ DC_DPAD_DOWN, "下" },
	{ DC_DPAD_LEFT, "左" },
	{ DC_DPAD_RIGHT, "右" },

	{ DC_AXIS_UP, "拇指摇杆 上" },
	{ DC_AXIS_DOWN, "拇指摇杆 下" },
	{ DC_AXIS_LEFT, "拇指摇杆 左" },
	{ DC_AXIS_RIGHT, "拇指摇杆 右" },

	{ DC_AXIS2_UP, "右.拇指摇杆 上" },
	{ DC_AXIS2_DOWN, "右.拇指摇杆 下" },
	{ DC_AXIS2_LEFT, "右.拇指摇杆 左" },
	{ DC_AXIS2_RIGHT, "右.拇指摇杆 右" },

	{ EMU_BTN_NONE, "按键" },
	{ DC_BTN_A, "按键 1" },
	{ DC_BTN_B, "按键 2" },
	{ DC_BTN_C, "按键 3" },
	{ DC_BTN_X, "按键 4" },
	{ DC_BTN_Y, "按键 5" },
	{ DC_BTN_Z, "按键 6" },
	{ DC_DPAD2_LEFT, "按键 7" },
	{ DC_DPAD2_RIGHT, "按键 8" },
//	{ DC_DPAD2_RIGHT, "Button 9" }, // TODO

	{ EMU_BTN_NONE, "触发器" },
	{ DC_AXIS_LT, "左触发器" },
	{ DC_AXIS_RT, "右触发器" },

	{ EMU_BTN_NONE, "系统按键" },
	{ DC_BTN_START, "开始" },
	{ DC_BTN_RELOAD, "Reload" },
	{ DC_BTN_D, "Coin" },
	{ DC_DPAD2_UP, "Service" },
	{ DC_DPAD2_DOWN, "Test" },

	{ EMU_BTN_NONE, "重新加载" },
	{ EMU_BTN_MENU, "菜单" },
	{ EMU_BTN_ESCAPE, "退出" },
	{ EMU_BTN_FFORWARD, "快进" },
	{ EMU_BTN_INSERT_CARD, "插入卡" },

	{ EMU_BTN_NONE, nullptr }
};

static MapleDeviceType maple_expansion_device_type_from_index(int idx)
{
	switch (idx)
	{
	case 1:
		return MDT_SegaVMU;
	case 2:
		return MDT_PurupuruPack;
	case 3:
		return MDT_Microphone;
	case 0:
	default:
		return MDT_None;
	}
}

static std::shared_ptr<GamepadDevice> mapped_device;
static u32 mapped_code;
static bool analogAxis;
static bool positiveDirection;
static double map_start_time;
static bool arcade_button_mode;
static u32 gamepad_port;

static void unmapControl(const std::shared_ptr<InputMapping>& mapping, u32 gamepad_port, DreamcastKey key)
{
	mapping->clear_button(gamepad_port, key);
	mapping->clear_axis(gamepad_port, key);
}

static DreamcastKey getOppositeDirectionKey(DreamcastKey key)
{
	switch (key)
	{
	case DC_DPAD_UP:
		return DC_DPAD_DOWN;
	case DC_DPAD_DOWN:
		return DC_DPAD_UP;
	case DC_DPAD_LEFT:
		return DC_DPAD_RIGHT;
	case DC_DPAD_RIGHT:
		return DC_DPAD_LEFT;
	case DC_DPAD2_UP:
		return DC_DPAD2_DOWN;
	case DC_DPAD2_DOWN:
		return DC_DPAD2_UP;
	case DC_DPAD2_LEFT:
		return DC_DPAD2_RIGHT;
	case DC_DPAD2_RIGHT:
		return DC_DPAD2_LEFT;
	case DC_AXIS_UP:
		return DC_AXIS_DOWN;
	case DC_AXIS_DOWN:
		return DC_AXIS_UP;
	case DC_AXIS_LEFT:
		return DC_AXIS_RIGHT;
	case DC_AXIS_RIGHT:
		return DC_AXIS_LEFT;
	case DC_AXIS2_UP:
		return DC_AXIS2_DOWN;
	case DC_AXIS2_DOWN:
		return DC_AXIS2_UP;
	case DC_AXIS2_LEFT:
		return DC_AXIS2_RIGHT;
	case DC_AXIS2_RIGHT:
		return DC_AXIS2_LEFT;
	default:
		return EMU_BTN_NONE;
	}
}
static void detect_input_popup(const Mapping *mapping)
{
	ImVec2 padding = ScaledVec2(20, 20);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, padding);
	if (ImGui::BeginPopupModal("映射控制", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		ImGui::Text("等待控制 '%s'...", mapping->name);
		double now = os_GetSeconds();
		ImGui::Text("暂停在 %d s", (int)(5 - (now - map_start_time)));
		if (mapped_code != (u32)-1)
		{
			std::shared_ptr<InputMapping> input_mapping = mapped_device->get_input_mapping();
			if (input_mapping != NULL)
			{
				unmapControl(input_mapping, gamepad_port, mapping->key);
				if (analogAxis)
				{
					input_mapping->set_axis(gamepad_port, mapping->key, mapped_code, positiveDirection);
					DreamcastKey opposite = getOppositeDirectionKey(mapping->key);
					// Map the axis opposite direction to the corresponding opposite dc button or axis,
					// but only if the opposite direction axis isn't used and the dc button or axis isn't mapped.
					if (opposite != EMU_BTN_NONE
							&& input_mapping->get_axis_id(gamepad_port, mapped_code, !positiveDirection) == EMU_BTN_NONE
							&& input_mapping->get_axis_code(gamepad_port, opposite).first == (u32)-1
							&& input_mapping->get_button_code(gamepad_port, opposite) == (u32)-1)
						input_mapping->set_axis(gamepad_port, opposite, mapped_code, !positiveDirection);
				}
				else
					input_mapping->set_button(gamepad_port, mapping->key, mapped_code);
			}
			mapped_device = NULL;
			ImGui::CloseCurrentPopup();
		}
		else if (now - map_start_time >= 5)
		{
			mapped_device = NULL;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
}

static void displayLabelOrCode(const char *label, u32 code, const char *suffix = "")
{
	if (label != nullptr)
		ImGui::Text("%s%s", label, suffix);
	else
		ImGui::Text("[%d]%s", code, suffix);
}

static void displayMappedControl(const std::shared_ptr<GamepadDevice>& gamepad, DreamcastKey key)
{
	std::shared_ptr<InputMapping> input_mapping = gamepad->get_input_mapping();
	u32 code = input_mapping->get_button_code(gamepad_port, key);
	if (code != (u32)-1)
	{
		displayLabelOrCode(gamepad->get_button_name(code), code);
		return;
	}
	std::pair<u32, bool> pair = input_mapping->get_axis_code(gamepad_port, key);
	code = pair.first;
	if (code != (u32)-1)
	{
		displayLabelOrCode(gamepad->get_axis_name(code), code, pair.second ? "+" : "-");
		return;
	}
}

static void controller_mapping_popup(const std::shared_ptr<GamepadDevice>& gamepad)
{
	fullScreenWindow(true);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	if (ImGui::BeginPopupModal("控制器映射", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float winWidth = ImGui::GetIO().DisplaySize.x - insetLeft - insetRight - (style.WindowBorderSize + style.WindowPadding.x) * 2;
		const float col_width = (winWidth - style.GrabMinSize - style.ItemSpacing.x
				- (ImGui::CalcTextSize("Map").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x)
				- (ImGui::CalcTextSize("Unmap").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x)) / 2;
		const float scaling = settings.display.uiScale;

		static int item_current_map_idx = 0;
		static int last_item_current_map_idx = 2;

		std::shared_ptr<InputMapping> input_mapping = gamepad->get_input_mapping();
		if (input_mapping == NULL || ImGui::Button("完成", ScaledVec2(100, 30)))
		{
			ImGui::CloseCurrentPopup();
			gamepad->save_mapping(map_system);
			last_item_current_map_idx = 2;
			ImGui::EndPopup();
			ImGui::PopStyleVar();
			return;
		}
		ImGui::SetItemDefaultFocus();

		float portWidth = 0;
		if (gamepad->maple_port() == MAPLE_PORTS)
		{
			ImGui::SameLine();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, (30 * scaling - ImGui::GetFontSize()) / 2));
			portWidth = ImGui::CalcTextSize("AA").x + ImGui::GetStyle().ItemSpacing.x * 2.0f + ImGui::GetFontSize();
			ImGui::SetNextItemWidth(portWidth);
			if (ImGui::BeginCombo("端口", maple_ports[gamepad_port + 1]))
			{
				for (u32 j = 0; j < MAPLE_PORTS; j++)
				{
					bool is_selected = gamepad_port == j;
					if (ImGui::Selectable(maple_ports[j + 1], &is_selected))
						gamepad_port = j;
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			portWidth += ImGui::CalcTextSize("端口").x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().FramePadding.x;
			ImGui::PopStyleVar();
		}
		float comboWidth = ImGui::CalcTextSize("Dreamcast控制").x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 4;
		float gameConfigWidth = 0;
		if (!settings.content.gameId.empty())
			gameConfigWidth = ImGui::CalcTextSize(gamepad->isPerGameMapping() ? "删除游戏配置" : "创建游戏配置").x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().FramePadding.x * 2;
		ImGui::SameLine(0, ImGui::GetContentRegionAvail().x - comboWidth - gameConfigWidth - ImGui::GetStyle().ItemSpacing.x - 100 * scaling * 2 - portWidth);

		ImGui::AlignTextToFramePadding();

		if (!settings.content.gameId.empty())
		{
			if (gamepad->isPerGameMapping())
			{
				if (ImGui::Button("删除游戏配置", ScaledVec2(0, 30)))
				{
					gamepad->setPerGameMapping(false);
					if (!gamepad->find_mapping(map_system))
						gamepad->resetMappingToDefault(arcade_button_mode, true);
				}
			}
			else
			{
				if (ImGui::Button("创建游戏配置", ScaledVec2(0, 30)))
					gamepad->setPerGameMapping(true);
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("重置...", ScaledVec2(100, 30)))
			ImGui::OpenPopup("确认重置");

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaledVec2(20, 20));
		if (ImGui::BeginPopupModal("确认重置", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		{
			ImGui::Text("是否确实要将映射重置为默认值？");
			static bool hitbox;
			if (arcade_button_mode)
			{
				ImGui::Text("控制器类型:");
				if (ImGui::RadioButton("游戏控制器", !hitbox))
					hitbox = false;
				ImGui::SameLine();
				if (ImGui::RadioButton("街机 / Hit Box", hitbox))
					hitbox = true;
			}
			ImGui::NewLine();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20 * scaling, ImGui::GetStyle().ItemSpacing.y));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(10, 10));
			if (ImGui::Button("Yes"))
			{
				gamepad->resetMappingToDefault(arcade_button_mode, !hitbox);
				gamepad->save_mapping(map_system);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("No"))
				ImGui::CloseCurrentPopup();
			ImGui::PopStyleVar(2);;

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar(1);;

		ImGui::SameLine();

		const char* items[] = { "Dreamcast控制", "街机控制" };

		// Here our selection data is an index.

		ImGui::SetNextItemWidth(comboWidth);
		// Make the combo height the same as the Done and Reset buttons
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, (30 * scaling - ImGui::GetFontSize()) / 2));
		ImGui::Combo("##arcadeMode", &item_current_map_idx, items, IM_ARRAYSIZE(items));
		ImGui::PopStyleVar();
		if (last_item_current_map_idx != 2 && item_current_map_idx != last_item_current_map_idx)
		{
			gamepad->save_mapping(map_system);
		}
		const Mapping *systemMapping = dcButtons;
		if (item_current_map_idx == 0)
		{
			arcade_button_mode = false;
			map_system = DC_PLATFORM_DREAMCAST;
			systemMapping = dcButtons;
		}
		else if (item_current_map_idx == 1)
		{
			arcade_button_mode = true;
			map_system = DC_PLATFORM_NAOMI;
			systemMapping = arcadeButtons;
		}

		if (item_current_map_idx != last_item_current_map_idx)
		{
			if (!gamepad->find_mapping(map_system))
				if (map_system == DC_PLATFORM_DREAMCAST || !gamepad->find_mapping(DC_PLATFORM_DREAMCAST))
					gamepad->resetMappingToDefault(arcade_button_mode, true);
			input_mapping = gamepad->get_input_mapping();

			last_item_current_map_idx = item_current_map_idx;
		}

		char key_id[32];

		ImGui::BeginChildFrame(ImGui::GetID("buttons"), ImVec2(0, 0), ImGuiWindowFlags_DragScrolling);

		for (; systemMapping->name != nullptr; systemMapping++)
		{
			if (systemMapping->key == EMU_BTN_NONE)
			{
				ImGui::Columns(1, nullptr, false);
				header(systemMapping->name);
				ImGui::Columns(3, "bindings", false);
				ImGui::SetColumnWidth(0, col_width);
				ImGui::SetColumnWidth(1, col_width);
				continue;
			}
			sprintf(key_id, "key_id%d", systemMapping->key);
			ImGui::PushID(key_id);

			const char *game_btn_name = nullptr;
			if (arcade_button_mode)
			{
				game_btn_name = GetCurrentGameButtonName(systemMapping->key);
				if (game_btn_name == nullptr)
					game_btn_name = GetCurrentGameAxisName(systemMapping->key);
			}
			if (game_btn_name != nullptr && game_btn_name[0] != '\0')
				ImGui::Text("%s - %s", systemMapping->name, game_btn_name);
			else
				ImGui::Text("%s", systemMapping->name);

			ImGui::NextColumn();
			displayMappedControl(gamepad, systemMapping->key);

			ImGui::NextColumn();
			if (ImGui::Button("映射"))
			{
				map_start_time = os_GetSeconds();
				ImGui::OpenPopup("映射控制");
				mapped_device = gamepad;
				mapped_code = -1;
				gamepad->detectButtonOrAxisInput([](u32 code, bool analog, bool positive)
						{
							mapped_code = code;
							analogAxis = analog;
							positiveDirection = positive;
						});
			}
			detect_input_popup(systemMapping);
			ImGui::SameLine();
			if (ImGui::Button("Unmap"))
			{
				input_mapping = gamepad->get_input_mapping();
				unmapControl(input_mapping, gamepad_port, systemMapping->key);
			}
			ImGui::NextColumn();
			ImGui::PopID();
		}
		ImGui::Columns(1, nullptr, false);
	    scrollWhenDraggingOnVoid();
	    windowDragScroll();

		ImGui::EndChildFrame();
		error_popup();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
}

void error_popup()
{
	if (!error_msg_shown && !error_msg.empty())
	{
		ImVec2 padding = ScaledVec2(20, 20);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, padding);
		ImGui::OpenPopup("Error");
		if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 400.f * settings.display.uiScale);
			ImGui::TextWrapped("%s", error_msg.c_str());
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(16, 3));
			float currentwidth = ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX((currentwidth - 80.f * settings.display.uiScale) / 2.f + ImGui::GetStyle().WindowPadding.x);
			if (ImGui::Button("OK", ScaledVec2(80.f, 0)))
			{
				error_msg.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::PopStyleVar();
			ImGui::PopTextWrapPos();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleVar();
		error_msg_shown = true;
	}
}

static void contentpath_warning_popup()
{
    static bool show_contentpath_selection;

    if (scanner.content_path_looks_incorrect)
    {
        ImGui::OpenPopup("内容位置不正确？");
        if (ImGui::BeginPopupModal("内容位置不正确？", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 400.f * settings.display.uiScale);
            ImGui::TextWrapped("  已扫描 %d 文件夹，但找不到游戏！  ", scanner.empty_folders_scanned);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(16, 3));
            float currentwidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((currentwidth - 100.f * settings.display.uiScale) / 2.f + ImGui::GetStyle().WindowPadding.x - 55.f * settings.display.uiScale);
            if (ImGui::Button("重新选择", ScaledVec2(100.f, 0)))
            {
            	scanner.content_path_looks_incorrect = false;
                ImGui::CloseCurrentPopup();
                show_contentpath_selection = true;
            }

            ImGui::SameLine();
            ImGui::SetCursorPosX((currentwidth - 100.f * settings.display.uiScale) / 2.f + ImGui::GetStyle().WindowPadding.x + 55.f * settings.display.uiScale);
            if (ImGui::Button("取消", ScaledVec2(100.f, 0)))
            {
            	scanner.content_path_looks_incorrect = false;
                ImGui::CloseCurrentPopup();
                scanner.stop();
                config::ContentPath.get().clear();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
    }
    if (show_contentpath_selection)
    {
        scanner.stop();
        ImGui::OpenPopup("选择目录");
        select_file_popup("选择目录", [](bool cancelled, std::string selection)
        {
            show_contentpath_selection = false;
            if (!cancelled)
            {
            	config::ContentPath.get().clear();
                config::ContentPath.get().push_back(selection);
            }
            scanner.refresh();
            return true;
        });
    }
}

static void gui_display_settings()
{
	static bool maple_devices_changed;

	fullScreenWindow(false);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

    ImGui::Begin("设置", NULL, ImGuiWindowFlags_DragScrolling | ImGuiWindowFlags_NoResize
    		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
	ImVec2 normal_padding = ImGui::GetStyle().FramePadding;

    if (ImGui::Button("完成", ScaledVec2(100, 30)))
    {
    	if (game_started)
    		gui_state = GuiState::Commands;
    	else
    		gui_state = GuiState::Main;
    	if (maple_devices_changed)
    	{
    		maple_devices_changed = false;
    		if (game_started && settings.platform.isConsole())
    		{
    			maple_ReconnectDevices();
    			reset_vmus();
    		}
    	}
       	SaveSettings();
    }
	if (game_started)
	{
	    ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16 * settings.display.uiScale, normal_padding.y));
		if (config::Settings::instance().hasPerGameConfig())
		{
			if (ImGui::Button("删除游戏配置", ScaledVec2(0, 30)))
			{
				config::Settings::instance().setPerGameConfig(false);
				config::Settings::instance().load(false);
				loadGameSpecificSettings();
			}
		}
		else
		{
			if (ImGui::Button("创建游戏配置", ScaledVec2(0, 30)))
				config::Settings::instance().setPerGameConfig(true);
		}
	    ImGui::PopStyleVar();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(16, 6));

    if (ImGui::BeginTabBar("设置", ImGuiTabBarFlags_NoTooltip))
    {
		if (ImGui::BeginTabItem("概述"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
			const char *languages[] = { "Japanese", "English", "German", "French", "Spanish", "Italian", "默认" };
			OptionComboBox("语言", config::Language, languages, ARRAY_SIZE(languages),
				"Dreamcast BIOS中配置的语言");

			const char *broadcast[] = { "NTSC", "PAL", "PAL/M", "PAL/N", "默认" };
			OptionComboBox("广播", config::Broadcast, broadcast, ARRAY_SIZE(broadcast),
					"非VGA模式的电视广播标准");

			const char *region[] = { "Japan", "USA", "Europe", "默认" };
			OptionComboBox("区域", config::Region, region, ARRAY_SIZE(region),
						"BIOS区域");

			const char *cable[] = { "VGA", "RGB Component", "TV Composite" };
			if (config::Cable.isReadOnly())
			{
		        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}
			if (ImGui::BeginCombo("缆绳", cable[config::Cable == 0 ? 0 : config::Cable - 1], ImGuiComboFlags_None))
			{
				for (int i = 0; i < IM_ARRAYSIZE(cable); i++)
				{
					bool is_selected = i == 0 ? config::Cable <= 1 : config::Cable - 1 == i;
					if (ImGui::Selectable(cable[i], &is_selected))
						config::Cable = i == 0 ? 0 : i + 1;
	                if (is_selected)
	                    ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (config::Cable.isReadOnly())
			{
		        ImGui::PopItemFlag();
		        ImGui::PopStyleVar();
			}
            ImGui::SameLine();
            ShowHelpMarker("视频连接类型");

#if !defined(TARGET_IPHONE)
            ImVec2 size;
            size.x = 0.0f;
            size.y = (ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.f)
            				* (config::ContentPath.get().size() + 1) ;//+ ImGui::GetStyle().FramePadding.y * 2.f;

            if (ImGui::ListBoxHeader("内容位置", size))
            {
            	int to_delete = -1;
                for (u32 i = 0; i < config::ContentPath.get().size(); i++)
                {
                	ImGui::PushID(config::ContentPath.get()[i].c_str());
                    ImGui::AlignTextToFramePadding();
                	ImGui::Text("%s", config::ContentPath.get()[i].c_str());
                	ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("X").x - ImGui::GetStyle().FramePadding.x);
                	if (ImGui::Button("X"))
                		to_delete = i;
                	ImGui::PopID();
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(24, 3));
                if (ImGui::Button("添加"))
                	ImGui::OpenPopup("选择目录");
                select_file_popup("选择目录", [](bool cancelled, std::string selection)
                		{
                			if (!cancelled)
                			{
                				scanner.stop();
                				config::ContentPath.get().push_back(selection);
                				scanner.refresh();
                			}
                			return true;
                		});
                ImGui::PopStyleVar();
                scrollWhenDraggingOnVoid();

        		ImGui::ListBoxFooter();
            	if (to_delete >= 0)
            	{
            		scanner.stop();
            		config::ContentPath.get().erase(config::ContentPath.get().begin() + to_delete);
        			scanner.refresh();
            	}
            }
            ImGui::SameLine();
            ShowHelpMarker("存储游戏的目录");

#if defined(__linux__) && !defined(__ANDROID__)
            if (ImGui::ListBoxHeader("数据目录", 1))
            {
            	ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", get_writable_data_path("").c_str());
                ImGui::ListBoxFooter();
            }
            ImGui::SameLine();
            ShowHelpMarker("包含BIOS文件以及保存的VMU和状态的目录");
#else
            if (ImGui::ListBoxHeader("主目录", 1))
            {
            	ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", get_writable_config_path("").c_str());
#ifdef __ANDROID__
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("更换").x - ImGui::GetStyle().FramePadding.x);
                if (ImGui::Button("更换"))
                	gui_state = GuiState::Onboarding;
#endif
                ImGui::ListBoxFooter();
            }
            ImGui::SameLine();
            ShowHelpMarker("Flycast保存配置文件和VMU的目录。BIOS文件应位于名为 \"data\" 的子文件夹中");
#endif // !linux
#endif // !TARGET_IPHONE

			if (OptionCheckbox("隐藏旧版 Naomi Roms", config::HideLegacyNaomiRoms,
					"从内容浏览器中隐藏.bin、.dat和.lst文件"))
				scanner.refresh();
	    	ImGui::Text("自动状态:");
			OptionCheckbox("加载", config::AutoLoadState,
					"启动时加载游戏的上次保存状态");
			ImGui::SameLine();
			OptionCheckbox("保存", config::AutoSaveState,
					"停止时保存游戏状态");
			OptionCheckbox("Naomi免费玩", config::ForceFreePlay, "在免费玩模式下配置Naomi游戏。");

			ImGui::PopStyleVar();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("控制"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
			header("物理设备");
		    {
				ImGui::Columns(4, "物理设备", false);
				ImVec4 gray{ 0.5f, 0.5f, 0.5f, 1.f };
				ImGui::TextColored(gray, "系统");
				ImGui::SetColumnWidth(-1, ImGui::CalcTextSize("系统").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x);
				ImGui::NextColumn();
				ImGui::TextColored(gray, "名字");
				ImGui::NextColumn();
				ImGui::TextColored(gray, "端口");
				ImGui::SetColumnWidth(-1, ImGui::CalcTextSize("没有").x * 1.6f + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight()
					+ ImGui::GetStyle().ItemInnerSpacing.x	+ ImGui::GetStyle().ItemSpacing.x);
				ImGui::NextColumn();
				ImGui::NextColumn();
				for (int i = 0; i < GamepadDevice::GetGamepadCount(); i++)
				{
					std::shared_ptr<GamepadDevice> gamepad = GamepadDevice::GetGamepad(i);
					if (!gamepad)
						continue;
					ImGui::Text("%s", gamepad->api_name().c_str());
					ImGui::NextColumn();
					ImGui::Text("%s", gamepad->name().c_str());
					ImGui::NextColumn();
					char port_name[32];
					sprintf(port_name, "##mapleport%d", i);
					ImGui::PushID(port_name);
					if (ImGui::BeginCombo(port_name, maple_ports[gamepad->maple_port() + 1]))
					{
						for (int j = -1; j < (int)ARRAY_SIZE(maple_ports) - 1; j++)
						{
							bool is_selected = gamepad->maple_port() == j;
							if (ImGui::Selectable(maple_ports[j + 1], &is_selected))
								gamepad->set_maple_port(j);
							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}

						ImGui::EndCombo();
					}
					ImGui::NextColumn();
					if (gamepad->remappable() && ImGui::Button("映射"))
					{
						gamepad_port = 0;
						ImGui::OpenPopup("控制器映射");
					}

					controller_mapping_popup(gamepad);

#ifdef __ANDROID__
					if (gamepad->is_virtual_gamepad())
					{
						if (ImGui::Button("编辑"))
						{
							vjoy_start_editing();
							gui_state = GuiState::VJoyEdit;
						}
						ImGui::SameLine();
						OptionSlider("触摸式", config::VirtualGamepadVibration, 0, 60);
					}
					else
#endif
					if (gamepad->is_rumble_enabled())
					{
						ImGui::SameLine(0, 16 * settings.display.uiScale);
						int power = gamepad->get_rumble_power();
						ImGui::SetNextItemWidth(150 * settings.display.uiScale);
						if (ImGui::SliderInt("Rumble", &power, 0, 100))
							gamepad->set_rumble_power(power);
					}
					ImGui::NextColumn();
					ImGui::PopID();
				}
		    }
	    	ImGui::Columns(1, NULL, false);

	    	ImGui::Spacing();
	    	OptionSlider("鼠标灵敏度", config::MouseSensitivity, 1, 500);
#if defined(_WIN32) && !defined(TARGET_UWP)
	    	OptionCheckbox("使用原始输入", config::UseRawInput, "支持多个定点设备（鼠标、光枪）和键盘");
#endif

			ImGui::Spacing();
			header("Dreamcast设备");
		    {
				for (int bus = 0; bus < MAPLE_PORTS; bus++)
				{
					ImGui::Text("设备 %c", bus + 'A');
					ImGui::SameLine();
					char device_name[32];
					sprintf(device_name, "##device%d", bus);
					float w = ImGui::CalcItemWidth() / 3;
					ImGui::PushItemWidth(w);
					if (ImGui::BeginCombo(device_name, maple_device_name(config::MapleMainDevices[bus]), ImGuiComboFlags_None))
					{
						for (int i = 0; i < IM_ARRAYSIZE(maple_device_types); i++)
						{
							bool is_selected = config::MapleMainDevices[bus] == maple_device_type_from_index(i);
							if (ImGui::Selectable(maple_device_types[i], &is_selected))
							{
								config::MapleMainDevices[bus] = maple_device_type_from_index(i);
								maple_devices_changed = true;
							}
							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					int port_count = config::MapleMainDevices[bus] == MDT_SegaController ? 2
							: config::MapleMainDevices[bus] == MDT_LightGun || config::MapleMainDevices[bus] == MDT_TwinStick || config::MapleMainDevices[bus] == MDT_AsciiStick ? 1
							: 0;
					for (int port = 0; port < port_count; port++)
					{
						ImGui::SameLine();
						sprintf(device_name, "##device%d.%d", bus, port + 1);
						ImGui::PushID(device_name);
						if (ImGui::BeginCombo(device_name, maple_expansion_device_name(config::MapleExpansionDevices[bus][port]), ImGuiComboFlags_None))
						{
							for (int i = 0; i < IM_ARRAYSIZE(maple_expansion_device_types); i++)
							{
								bool is_selected = config::MapleExpansionDevices[bus][port] == maple_expansion_device_type_from_index(i);
								if (ImGui::Selectable(maple_expansion_device_types[i], &is_selected))
								{
									config::MapleExpansionDevices[bus][port] = maple_expansion_device_type_from_index(i);
									maple_devices_changed = true;
								}
								if (is_selected)
									ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
						ImGui::PopID();
					}
					if (config::MapleMainDevices[bus] == MDT_LightGun)
					{
						ImGui::SameLine();
						sprintf(device_name, "##device%d.xhair", bus);
						ImGui::PushID(device_name);
						u32 color = config::CrosshairColor[bus];
						float xhairColor[4] {
							(color & 0xff) / 255.f,
							((color >> 8) & 0xff) / 255.f,
							((color >> 16) & 0xff) / 255.f,
							((color >> 24) & 0xff) / 255.f
						};
						ImGui::ColorEdit4("Crosshair color", xhairColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf
								| ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoLabel);
						ImGui::SameLine();
						bool enabled = color != 0;
						ImGui::Checkbox("Crosshair", &enabled);
						if (enabled)
						{
							config::CrosshairColor[bus] = (u8)(xhairColor[0] * 255.f)
									| ((u8)(xhairColor[1] * 255.f) << 8)
									| ((u8)(xhairColor[2] * 255.f) << 16)
									| ((u8)(xhairColor[3] * 255.f) << 24);
							if (config::CrosshairColor[bus] == 0)
								config::CrosshairColor[bus] = 0xC0FFFFFF;
						}
						else
						{
							config::CrosshairColor[bus] = 0;
						}
						ImGui::PopID();
					}
					ImGui::PopItemWidth();
				}
		    }

			ImGui::PopStyleVar();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("画面"))
		{
			int renderApi;
			bool perPixel;
			switch (config::RendererType)
			{
			default:
			case RenderType::OpenGL:
				renderApi = 0;
				perPixel = false;
				break;
			case RenderType::OpenGL_OIT:
				renderApi = 0;
				perPixel = true;
				break;
			case RenderType::Vulkan:
				renderApi = 1;
				perPixel = false;
				break;
			case RenderType::Vulkan_OIT:
				renderApi = 1;
				perPixel = true;
				break;
			case RenderType::DirectX9:
				renderApi = 2;
				perPixel = false;
				break;
			case RenderType::DirectX11:
				renderApi = 3;
				perPixel = false;
				break;
			case RenderType::DirectX11_OIT:
				renderApi = 3;
				perPixel = true;
				break;
			}

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
			const bool has_per_pixel = GraphicsContext::Instance()->hasPerPixel();
		    header("透明排序");
		    {
		    	int renderer = perPixel ? 2 : config::PerStripSorting ? 1 : 0;
		    	ImGui::Columns(has_per_pixel ? 3 : 2, "渲染器", false);
		    	ImGui::RadioButton("Per三角", &renderer, 0);
	            ImGui::SameLine();
	            ShowHelpMarker("对每个三角形的透明多边形进行排序。速度快，但可能会产生图形故障");
            	ImGui::NextColumn();
		    	ImGui::RadioButton("Per条形", &renderer, 1);
	            ImGui::SameLine();
	            ShowHelpMarker("对每条透明多边形进行排序。更快，但可能产生图形故障");
	            if (has_per_pixel)
	            {
	            	ImGui::NextColumn();
	            	ImGui::RadioButton("Per像素", &renderer, 2);
	            	ImGui::SameLine();
	            	ShowHelpMarker("对每个像素的透明多边形进行排序。速度较慢但准确");
	            }
		    	ImGui::Columns(1, NULL, false);
		    	switch (renderer)
		    	{
		    	case 0:
		    		perPixel = false;
		    		config::PerStripSorting.set(false);
		    		break;
		    	case 1:
		    		perPixel = false;
		    		config::PerStripSorting.set(true);
		    		break;
		    	case 2:
		    		perPixel = true;
		    		break;
		    	}
		    }
	    	ImGui::Spacing();
		    header("渲染选项");
		    {
		    	ImGui::Text("自动跳过帧:");
		    	ImGui::Columns(3, "自动跳过", false);
		    	OptionRadioButton("禁用", config::AutoSkipFrame, 0, "不跳帧");
            	ImGui::NextColumn();
		    	OptionRadioButton("正常的", config::AutoSkipFrame, 1, "当GPU和CPU都运行较慢时，跳过一帧");
            	ImGui::NextColumn();
		    	OptionRadioButton("最大", config::AutoSkipFrame, 2, "当GPU运行较慢时，跳过一帧");
		    	ImGui::Columns(1, nullptr, false);

		    	OptionCheckbox("阴影", config::ModifierVolumes,
		    			"启用修改器音量，通常用于阴影");
		    	OptionCheckbox("雾", config::Fog, "启用雾效果");
		    	OptionCheckbox("宽屏", config::Widescreen,
		    			"在正常的4:3长宽比之外绘制几何图形。可能在显示区域产生图形故障");
		    	if (!config::Widescreen)
		    	{
			        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		    	}
		    	ImGui::Indent();
		    	OptionCheckbox("超宽屏", config::SuperWidescreen,
		    			"当屏幕或窗口的宽高比大于16:9时，使用全宽");
		    	ImGui::Unindent();
		    	if (!config::Widescreen)
		    	{
			        ImGui::PopItemFlag();
			        ImGui::PopStyleVar();
		    	}
		    	OptionCheckbox("宽屏游戏作弊", config::WidescreenGameHacks,
		    			"修改游戏，使其以16:9变形格式显示，并使用水平屏幕拉伸。只支持部分游戏。");

				const std::array<float, 5> aniso{ 1, 2, 4, 8, 16 };
	            const std::array<std::string, 5> anisoText{ "禁用", "2x", "4x", "8x", "16x" };
	            u32 afSelected = 0;
	            for (u32 i = 0; i < aniso.size(); i++)
	            {
	            	if (aniso[i] == config::AnisotropicFiltering)
	            		afSelected = i;
	            }

                ImGuiStyle& style = ImGui::GetStyle();
                float innerSpacing = style.ItemInnerSpacing.x;
                ImGui::PushItemWidth(ImGui::CalcItemWidth() - innerSpacing * 2.0f - ImGui::GetFrameHeight() * 2.0f);
                if (ImGui::BeginCombo("##各向异性过滤", anisoText[afSelected].c_str(), ImGuiComboFlags_NoArrowButton))
                {
                	for (u32 i = 0; i < aniso.size(); i++)
                    {
                        bool is_selected = aniso[i] == config::AnisotropicFiltering;
                        if (ImGui::Selectable(anisoText[i].c_str(), is_selected))
                        	config::AnisotropicFiltering = aniso[i];
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::SameLine(0, innerSpacing);

                if (ImGui::ArrowButton("##减少各向异性滤波", ImGuiDir_Left))
                {
                    if (afSelected > 0)
                    	config::AnisotropicFiltering = aniso[afSelected - 1];
                }
                ImGui::SameLine(0, innerSpacing);
                if (ImGui::ArrowButton("##增加各向异性滤波", ImGuiDir_Right))
                {
                    if (afSelected < aniso.size() - 1)
                    	config::AnisotropicFiltering = aniso[afSelected + 1];
                }
                ImGui::SameLine(0, style.ItemInnerSpacing.x);

                ImGui::Text("各向异性过滤");
                ImGui::SameLine();
                ShowHelpMarker("更高的值使以倾斜角度查看的纹理看起来更清晰，但对GPU的要求更高。此选项仅对mipmap纹理有可见影响。");

		    	ImGui::Text("纹理过滤:");
		    	ImGui::Columns(3, "纹理过滤", false);
		    	OptionRadioButton("默认", config::TextureFiltering, 0, "使用游戏默认的纹理过滤");
            	ImGui::NextColumn();
		    	OptionRadioButton("强制最近邻居", config::TextureFiltering, 1, "对所有纹理强制最近邻居过滤。外观更清晰，但可能会导致各种渲染问题。此选项通常不会影响性能。");
            	ImGui::NextColumn();
		    	OptionRadioButton("强制线性", config::TextureFiltering, 2, "对所有纹理强制进行线性过滤。外观更平滑，但可能会导致各种渲染问题。此选项通常不会影响性能。");
		    	ImGui::Columns(1, nullptr, false);

#ifndef TARGET_IPHONE
		    	OptionCheckbox("垂直同步", config::VSync, "同步帧速率与屏幕刷新率。推荐");
		    	ImGui::Indent();
		    	if (!config::VSync || !isVulkan(config::RendererType))
		    	{
			        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		    	}
	    		OptionCheckbox("复制帧", config::DupeFrames, "在高刷新率监视器上重复帧(120赫兹或更高)");
		    	if (!config::VSync || !isVulkan(config::RendererType))
		    	{
			        ImGui::PopItemFlag();
			        ImGui::PopStyleVar();
		    	}
		    	ImGui::Unindent();
#endif
		    	OptionCheckbox("显示FPS计数器", config::ShowFPS, "显示屏幕上的帧/秒计数器");
		    	OptionCheckbox("在游戏中显示VMU", config::FloatVMUs, "在游戏中显示VMU LCD屏幕");
		    	OptionCheckbox("旋转屏幕90°", config::Rotate90, "逆时针旋转屏幕90°");
		    	OptionCheckbox("延迟帧交换", config::DelayFrameSwapping,
		    			"有助于避免屏幕闪烁或视频故障。不建议在慢速平台上使用");
		    	constexpr int apiCount = 0
					#ifdef USE_VULKAN
		    			+ 1
					#endif
					#ifdef USE_DX9
						+ 1
					#endif
					#ifdef USE_OPENGL
						+ 1
					#endif
					#ifdef _WIN32
						+ 1
					#endif
						;

		    	if (apiCount > 1)
		    	{
		    		ImGui::Text("图形API:");
					ImGui::Columns(apiCount, "渲染Api", false);
#ifdef USE_OPENGL
					ImGui::RadioButton("Open GL", &renderApi, 0);
					ImGui::NextColumn();
#endif
#ifdef USE_VULKAN
					ImGui::RadioButton("Vulkan", &renderApi, 1);
					ImGui::NextColumn();
#endif
#ifdef USE_DX9
					ImGui::RadioButton("DirectX 9", &renderApi, 2);
					ImGui::NextColumn();
#endif
#ifdef _WIN32
					ImGui::RadioButton("DirectX 11", &renderApi, 3);
					ImGui::NextColumn();
#endif
					ImGui::Columns(1, nullptr, false);
		    	}

	            const std::array<float, 13> scalings{ 0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 4.f, 4.5f, 5.f, 6.f, 7.f, 8.f, 9.f };
	            const std::array<std::string, 13> scalingsText{ "一半", "本地", "x1.5", "x2", "x2.5", "x3", "x4", "x4.5", "x5", "x6", "x7", "x8", "x9" };
	            std::array<int, scalings.size()> vres;
	            std::array<std::string, scalings.size()> resLabels;
	            u32 selected = 0;
	            for (u32 i = 0; i < scalings.size(); i++)
	            {
	            	vres[i] = scalings[i] * 480;
	            	if (vres[i] == config::RenderResolution)
	            		selected = i;
	            	if (!config::Widescreen)
	            		resLabels[i] = std::to_string((int)(scalings[i] * 640)) + "x" + std::to_string((int)(scalings[i] * 480));
	            	else
	            		resLabels[i] = std::to_string((int)(scalings[i] * 480 * 16 / 9)) + "x" + std::to_string((int)(scalings[i] * 480));
	            	resLabels[i] += " (" + scalingsText[i] + ")";
	            }

                ImGui::PushItemWidth(ImGui::CalcItemWidth() - innerSpacing * 2.0f - ImGui::GetFrameHeight() * 2.0f);
                if (ImGui::BeginCombo("##决议", resLabels[selected].c_str(), ImGuiComboFlags_NoArrowButton))
                {
                	for (u32 i = 0; i < scalings.size(); i++)
                    {
                        bool is_selected = vres[i] == config::RenderResolution;
                        if (ImGui::Selectable(resLabels[i].c_str(), is_selected))
                        	config::RenderResolution = vres[i];
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::SameLine(0, innerSpacing);

                if (ImGui::ArrowButton("##减少分辨率", ImGuiDir_Left))
                {
                    if (selected > 0)
                    	config::RenderResolution = vres[selected - 1];
                }
                ImGui::SameLine(0, innerSpacing);
                if (ImGui::ArrowButton("##增加分辨率", ImGuiDir_Right))
                {
                    if (selected < vres.size() - 1)
                    	config::RenderResolution = vres[selected + 1];
                }
                ImGui::SameLine(0, style.ItemInnerSpacing.x);

                ImGui::Text("内部分辨率");
                ImGui::SameLine();
                ShowHelpMarker("内部渲染分辨率。越高越好，但对GPU的要求越高。可以使用高于显示分辨率的值（但不超过显示分辨率的两倍）进行超级采样，这样可以在不降低清晰度的情况下提供高质量的抗锯齿。");

		    	OptionSlider("水平拉伸", config::ScreenStretching, 100, 150,
		    			"水平拉伸屏幕");
		    	OptionArrowButtons("跳帧", config::SkipFrame, 0, 6,
		    			"两个实际渲染帧之间要跳过的帧数");
		    }
	    	ImGui::Spacing();
		    header("渲染到纹理");
		    {
		    	OptionCheckbox("复制到VRAM", config::RenderToTextureBuffer,
		    			"将渲染的纹理复制回VRAM。速度较慢但准确");
		    }
	    	ImGui::Spacing();
		    header("纹理放大");
		    {
#ifndef TARGET_NO_OPENMP
		    	OptionArrowButtons("纹理放大", config::TextureUpscale, 1, 8,
		    			"使用xBRZ算法放大纹理。仅适用于快速平台和某些2D游戏");
		    	OptionSlider("纹理最大大小", config::MaxFilteredTextureSize, 8, 1024,
		    			"大于此尺寸平方的纹理将不会放大");
		    	OptionArrowButtons("最大线程数", config::MaxThreads, 1, 8,
		    			"用于纹理放大的最大线程数。建议：物理核数减1");
#endif
		    	OptionCheckbox("加载自定义纹理", config::CustomTextures,
		    			"从data/textures/<game id>加载自定义/高分辨率纹理");
		    }
			ImGui::PopStyleVar();
			ImGui::EndTabItem();

		    switch (renderApi)
		    {
		    case 0:
		    	config::RendererType = perPixel ? RenderType::OpenGL_OIT : RenderType::OpenGL;
		    	break;
		    case 1:
		    	config::RendererType = perPixel ? RenderType::Vulkan_OIT : RenderType::Vulkan;
		    	break;
		    case 2:
		    	config::RendererType = RenderType::DirectX9;
		    	break;
		    case 3:
		    	config::RendererType = perPixel ? RenderType::DirectX11_OIT : RenderType::DirectX11;
		    	break;
		    }
		}
		if (ImGui::BeginTabItem("音频"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
			OptionCheckbox("启用 DSP", config::DSPEnabled,
					"启用Dreamcast数字声音处理器。只推荐在快速平台上使用");
			if (OptionSlider("音量级别", config::AudioVolume, 0, 100, "调整模拟器的音频级别"))
			{
				config::AudioVolume.calcDbPower();
			};
#ifdef __ANDROID__
			if (config::AudioBackend.get() == "auto" || config::AudioBackend.get() == "android")
				OptionCheckbox("自动延迟", config::AutoLatency,
						"自动设置音频延迟。推荐");
#endif
            if (!config::AutoLatency
            		|| (config::AudioBackend.get() != "auto" && config::AudioBackend.get() != "android"))
            {
				int latency = (int)roundf(config::AudioBufferSize * 1000.f / 44100.f);
				ImGui::SliderInt("延迟", &latency, 12, 512, "%d ms");
				config::AudioBufferSize = (int)roundf(latency * 44100.f / 1000.f);
				ImGui::SameLine();
				ShowHelpMarker("设置最大音频延迟。并非所有音频驱动程序都支持。");
            }

			audiobackend_t* backend = nullptr;
			std::string backend_name = config::AudioBackend;
			if (backend_name != "auto")
			{
				backend = GetAudioBackend(config::AudioBackend);
				if (backend != NULL)
					backend_name = backend->slug;
			}

			audiobackend_t* current_backend = backend;
			if (ImGui::BeginCombo("音频驱动程序", backend_name.c_str(), ImGuiComboFlags_None))
			{
				bool is_selected = (config::AudioBackend.get() == "auto");
				if (ImGui::Selectable("auto -自动选择驱动程序", &is_selected))
					config::AudioBackend.set("auto");

				for (u32 i = 0; i < GetAudioBackendCount(); i++)
				{
					backend = GetAudioBackend(i);
					is_selected = (config::AudioBackend.get() == backend->slug);

					if (is_selected)
						current_backend = backend;

					if (ImGui::Selectable((backend->slug + " - " + backend->name).c_str(), &is_selected))
						config::AudioBackend.set(backend->slug);
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ShowHelpMarker("要使用的音频驱动程序");

			if (current_backend != NULL && current_backend->get_options != NULL)
			{
				// get backend specific options
				int option_count;
				audio_option_t* options = current_backend->get_options(&option_count);

				for (int o = 0; o < option_count; o++)
				{
					std::string value = cfgLoadStr(current_backend->slug, options->cfg_name, "");

					if (options->type == integer)
					{
						int val = stoi(value);
						if (ImGui::SliderInt(options->caption.c_str(), &val, options->min_value, options->max_value))
						{
							std::string s = std::to_string(val);
							cfgSaveStr(current_backend->slug, options->cfg_name, s);
						}
					}
					else if (options->type == checkbox)
					{
						bool check = value == "1";
						if (ImGui::Checkbox(options->caption.c_str(), &check))
							cfgSaveStr(current_backend->slug, options->cfg_name,
									check ? "1" : "0");
					}
					else if (options->type == ::list)
					{
						if (ImGui::BeginCombo(options->caption.c_str(), value.c_str(), ImGuiComboFlags_None))
						{
							bool is_selected = false;
							for (const auto& cur : options->list_callback())
							{
								is_selected = value == cur;
								if (ImGui::Selectable(cur.c_str(), &is_selected))
									cfgSaveStr(current_backend->slug, options->cfg_name, cur);

								if (is_selected)
									ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
					}
					else {
						WARN_LOG(RENDERER, "未知选项");
					}

					options++;
				}
			}

			ImGui::PopStyleVar();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("高级"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
		    header("CPU 模式");
		    {
				ImGui::Columns(2, "cpu_modes", false);
				OptionRadioButton("动态编译", config::DynarecEnabled, true,
					"使用动态重新编译程序。大多数情况下推荐");
				ImGui::NextColumn();
				OptionRadioButton("翻译", config::DynarecEnabled, false,
					"使用解释器。非常慢，但可能有助于解决动态问题");
				ImGui::Columns(1, NULL, false);
		    }
		    if (config::DynarecEnabled)
		    {
		    	ImGui::Spacing();
		    	header("动态编译设置");
		    	OptionCheckbox("闲置跳过", config::DynarecIdleSkip, "跳过等待循环。推荐");
		    }
	    	ImGui::Spacing();
		    header("网络");
		    {
		    	OptionCheckbox("宽带适配器仿真", config::EmulateBBA,
		    			"模拟以太网宽带适配器(BBA)而不是调制解调器");
		    	OptionCheckbox("启用GGPO组网", config::GGPOEnable,
		    			"启用GGPO组网");
		    	OptionCheckbox("启用Naomi网络", config::NetworkEnable,
		    			"为支持的Naomi游戏启用网络");
		    	if (config::GGPOEnable)
		    	{
		    		config::NetworkEnable = false;
					OptionCheckbox("以玩家1身份玩游戏", config::ActAsServer,
							"取消选择作为玩家2进行游戏");
					char server_name[256];
					strcpy(server_name, config::NetworkServer.get().c_str());
					ImGui::InputText("同行", server_name, sizeof(server_name), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
					ImGui::SameLine();
					ShowHelpMarker("您的对等IP地址和可选端口");
					config::NetworkServer.set(server_name);
					OptionSlider("帧延迟", config::GGPODelay, 0, 20,
						"设置帧延迟，建议使用ping >100毫秒的会话");

					ImGui::Text("左侧指杆:");
					OptionRadioButton<int>("禁用", config::GGPOAnalogAxes, 0, "左手拇指杆未使用");
					ImGui::SameLine();
					OptionRadioButton<int>("水平", config::GGPOAnalogAxes, 1, "只使用左拇指杆水平轴");
					ImGui::SameLine();
					OptionRadioButton<int>("完整", config::GGPOAnalogAxes, 2, "使用左手拇指杆的水平和垂直轴");

					OptionCheckbox("启用聊天", config::GGPOChat, "收到聊天信息后打开聊天窗口");
					OptionCheckbox("网络统计信息", config::NetworkStats,
			    			"在屏幕上显示网络统计信息");
		    	}
		    	else if (config::NetworkEnable)
		    	{
					OptionCheckbox("充当服务器", config::ActAsServer,
							"为Naomi网络游戏创建本地服务器");
					if (!config::ActAsServer)
					{
						char server_name[256];
						strcpy(server_name, config::NetworkServer.get().c_str());
						ImGui::InputText("服务器", server_name, sizeof(server_name), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
						ImGui::SameLine();
						ShowHelpMarker("要连接的服务器。保留为空将自动在默认端口上查找服务器");
						config::NetworkServer.set(server_name);
					}
					char localPort[256];
					sprintf(localPort, "%d", (int)config::LocalPort);
					ImGui::InputText("本地端口", localPort, sizeof(localPort), ImGuiInputTextFlags_CharsDecimal, nullptr, nullptr);
					ImGui::SameLine();
					ShowHelpMarker("要使用的本地UDP端口");
					config::LocalPort.set(atoi(localPort));
		    	}
				OptionCheckbox("启用UPnP", config::EnableUPnP);
				ImGui::SameLine();
				ShowHelpMarker("为网络游戏自动配置网络路由器");
		    }
	    	ImGui::Spacing();
		    header("其他");
		    {
		    	OptionCheckbox("HLE BIOS", config::UseReios, "强制高级BIOS仿真");
	            OptionCheckbox("强制Windows CE", config::ForceWindowsCE,
	            		"启用完整的MMU模拟和其他Windows CE设置。除非必要，否则不要启用");
	            OptionCheckbox("多线程仿真", config::ThreadedRendering,
	            		"在不同的线程上运行模拟的CPU和GPU");
#ifndef __ANDROID
	            OptionCheckbox("串行控制台", config::SerialConsole,
	            		"将Dreamcast串行控制台转储到stdout");
#endif
	            OptionCheckbox("转储纹理", config::DumpTextures,
	            		"将所有纹理转储到data/texdump/<game id>");

	            bool logToFile = cfgLoadBool("log", "LogToFile", false);
	            bool newLogToFile = logToFile;
				ImGui::Checkbox("Log to File", &newLogToFile);
				if (logToFile != newLogToFile)
				{
					cfgSaveBool("log", "LogToFile", newLogToFile);
					LogManager::Shutdown();
					LogManager::Init();
				}
	            ImGui::SameLine();
	            ShowHelpMarker("Log debug information to flycast.log");
		    }
			ImGui::PopStyleVar();
			ImGui::EndTabItem();

			#ifdef USE_LUA
			header("Lua Scripting");
			{
				char LuaFileName[256];

				strcpy(LuaFileName, config::LuaFileName.get().c_str());
				ImGui::InputText("Lua Filename", LuaFileName, sizeof(LuaFileName), ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
				ImGui::SameLine();
				ShowHelpMarker("Specify lua filename to use. Should be located in Flycast config directory. Defaults to flycast.lua when empty.");
				config::LuaFileName = LuaFileName;

			}
			#endif
		}
		if (ImGui::BeginTabItem("About"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, normal_padding);
		    header("Flycast");
		    {
				ImGui::Text("Version: %s", GIT_VERSION);
				ImGui::Text("Git Hash: %s", GIT_HASH);
				ImGui::Text("Build Date: %s", BUILD_DATE);
		    }
	    	ImGui::Spacing();
		    header("Platform");
		    {
		    	ImGui::Text("CPU: %s",
#if HOST_CPU == CPU_X86
					"x86"
#elif HOST_CPU == CPU_ARM
					"ARM"
#elif HOST_CPU == CPU_MIPS
					"MIPS"
#elif HOST_CPU == CPU_X64
					"x86/64"
#elif HOST_CPU == CPU_GENERIC
					"Generic"
#elif HOST_CPU == CPU_ARM64
					"ARM64"
#else
					"Unknown"
#endif
						);
		    	ImGui::Text("Operating System: %s",
#ifdef __ANDROID__
					"Android"
#elif defined(__unix__)
					"Linux"
#elif defined(__APPLE__)
#ifdef TARGET_IPHONE
		    		"iOS"
#else
					"macOS"
#endif
#elif defined(TARGET_UWP)
					"Windows Universal Platform"
#elif defined(_WIN32)
					"Windows"
#elif defined(__SWITCH__)
					"Switch"
#else
					"Unknown"
#endif
						);
#ifdef TARGET_IPHONE
				extern std::string iosJitStatus;
				ImGui::Text("JIT Status: %s", iosJitStatus.c_str());
#endif
		    }
	    	ImGui::Spacing();
	    	if (isOpenGL(config::RendererType))
				header("Open GL");
	    	else if (isVulkan(config::RendererType))
				header("Vulkan");
	    	else if (isDirectX(config::RendererType))
				header("DirectX");
			ImGui::Text("Driver Name: %s", GraphicsContext::Instance()->getDriverName().c_str());
			ImGui::Text("Version: %s", GraphicsContext::Instance()->getDriverVersion().c_str());

#ifdef __ANDROID__
		    ImGui::Separator();
		    if (ImGui::Button("Send Logs")) {
		    	void android_send_logs();
		    	android_send_logs();
		    }
#endif
			ImGui::PopStyleVar();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();

    scrollWhenDraggingOnVoid();
    windowDragScroll();
    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_display_notification(const char *msg, int duration)
{
	std::lock_guard<std::mutex> lock(osd_message_mutex);
	osd_message = msg;
	osd_message_end = os_GetSeconds() + (double)duration / 1000.0;
}

static std::string get_notification()
{
	std::lock_guard<std::mutex> lock(osd_message_mutex);
	if (!osd_message.empty() && os_GetSeconds() >= osd_message_end)
		osd_message.clear();
	return osd_message;
}

inline static void gui_display_demo()
{
	ImGui::ShowDemoWindow();
}

static void gui_display_content()
{
	fullScreenWindow(false);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

    ImGui::Begin("##main", NULL, ImGuiWindowFlags_NoDecoration);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(20, 8));
    ImGui::AlignTextToFramePadding();
    ImGui::Indent(10 * settings.display.uiScale);
    ImGui::Text("GAMES");
    ImGui::Unindent(10 * settings.display.uiScale);

    static ImGuiTextFilter filter;
#if !defined(__ANDROID__) && !defined(TARGET_IPHONE) && !defined(TARGET_UWP)
	ImGui::SameLine(0, 32 * settings.display.uiScale);
	filter.Draw("Filter");
#endif
    if (gui_state != GuiState::SelectDisk)
    {
#ifdef TARGET_UWP
    	void gui_load_game();
		ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Settings").x - ImGui::GetStyle().FramePadding.x * 4.0f  - ImGui::GetStyle().ItemSpacing.x - ImGui::CalcTextSize("Load...").x);
		if (ImGui::Button("Load..."))
			gui_load_game();
		ImGui::SameLine();
#else
		ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Settings").x - ImGui::GetStyle().FramePadding.x * 2.0f);
#endif
		if (ImGui::Button("Settings"))
			gui_state = GuiState::Settings;
    }
    ImGui::PopStyleVar();

    scanner.fetch_game_list();

	// Only if Filter and Settings aren't focused... ImGui::SetNextWindowFocus();
	ImGui::BeginChild(ImGui::GetID("library"), ImVec2(0, 0), true, ImGuiWindowFlags_DragScrolling);
    {
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ScaledVec2(8, 20));

		ImGui::PushID("bios");
		if (ImGui::Selectable("Dreamcast BIOS"))
			gui_start_game("");
		ImGui::PopID();

		{
			scanner.get_mutex().lock();
			for (const auto& game : scanner.get_game_list())
			{
				if (gui_state == GuiState::SelectDisk)
				{
					std::string extension = get_file_extension(game.path);
					if (extension != "gdi" && extension != "chd"
							&& extension != "cdi" && extension != "cue")
						// Only dreamcast disks
						continue;
				}
				if (filter.PassFilter(game.name.c_str()))
				{
					ImGui::PushID(game.path.c_str());
					if (ImGui::Selectable(game.name.c_str()))
					{
						if (gui_state == GuiState::SelectDisk)
						{
							settings.content.path = game.path;
							try {
								DiscSwap(game.path);
								gui_state = GuiState::Closed;
							} catch (const FlycastException& e) {
								gui_error(e.what());
							}
						}
						else
						{
							std::string gamePath(game.path);
							scanner.get_mutex().unlock();
							gui_start_game(gamePath);
							scanner.get_mutex().lock();
							ImGui::PopID();
							break;
						}
					}
					ImGui::PopID();
				}
			}
			scanner.get_mutex().unlock();
		}
        ImGui::PopStyleVar();
    }
    windowDragScroll();
	ImGui::EndChild();
	ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    contentpath_warning_popup();
}

static bool systemdir_selected_callback(bool cancelled, std::string selection)
{
	if (cancelled)
	{
		gui_state = GuiState::Main;
		return true;
	}
	selection += "/";

	std::string data_path = selection + "data/";
	if (!file_exists(data_path))
	{
		if (!make_directory(data_path))
		{
			WARN_LOG(BOOT, "Cannot create 'data' directory: %s", data_path.c_str());
			gui_error("Invalid selection:\nFlycast cannot write to this directory.");
			return false;
		}
	}
	else
	{
		// Test
		std::string testPath = data_path + "writetest.txt";
		FILE *file = fopen(testPath.c_str(), "w");
		if (file == nullptr)
		{
			WARN_LOG(BOOT, "Cannot write in the 'data' directory");
			gui_error("Invalid selection:\nFlycast cannot write to this directory.");
			return false;
		}
		fclose(file);
		unlink(testPath.c_str());
	}
	set_user_config_dir(selection);
	add_system_data_dir(selection);
	set_user_data_dir(data_path);

	if (cfgOpen())
	{
		config::Settings::instance().load(false);
		// Make sure the renderer type doesn't change mid-flight
		config::RendererType = RenderType::OpenGL;
		gui_state = GuiState::Main;
		if (config::ContentPath.get().empty())
		{
			scanner.stop();
			config::ContentPath.get().push_back(selection);
		}
		SaveSettings();
	}
	return true;
}

static void gui_display_onboarding()
{
	ImGui::OpenPopup("Select System Directory");
	select_file_popup("Select System Directory", &systemdir_selected_callback);
}

static std::future<bool> networkStatus;

static void gui_network_start()
{
	centerNextWindow();
	ImGui::SetNextWindowSize(ScaledVec2(330, 180));

	ImGui::Begin("##network", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(20, 10));
	ImGui::AlignTextToFramePadding();
	ImGui::SetCursorPosX(20.f * settings.display.uiScale);

	if (networkStatus.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		ImGui::Text("Starting...");
		try {
			if (networkStatus.get())
			{
				gui_state = GuiState::Closed;
			}
			else
			{
				emu.unloadGame();
				gui_state = GuiState::Main;
			}
		} catch (const FlycastException& e) {
			NetworkHandshake::instance->stop();
			emu.unloadGame();
			gui_error(e.what());
			gui_state = GuiState::Main;
		}
	}
	else
	{
		ImGui::Text("Starting Network...");
		if (NetworkHandshake::instance->canStartNow())
			ImGui::Text("Press Start to start the game now.");
	}
	ImGui::Text("%s", get_notification().c_str());

	float currentwidth = ImGui::GetContentRegionAvail().x;
	ImGui::SetCursorPosX((currentwidth - 100.f * settings.display.uiScale) / 2.f + ImGui::GetStyle().WindowPadding.x);
	ImGui::SetCursorPosY(126.f * settings.display.uiScale);
	if (ImGui::Button("Cancel", ScaledVec2(100.f, 0)))
	{
		NetworkHandshake::instance->stop();
		try {
			networkStatus.get();
		}
		catch (const FlycastException& e) {
		}
		emu.unloadGame();
		gui_state = GuiState::Main;
	}
	ImGui::PopStyleVar();

	ImGui::End();

	if ((kcode[0] & DC_BTN_START) == 0)
		NetworkHandshake::instance->startNow();
}

static void gui_display_loadscreen()
{
	centerNextWindow();
	ImGui::SetNextWindowSize(ScaledVec2(330, 180));

    ImGui::Begin("##loading", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaledVec2(20, 10));
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(20.f * settings.display.uiScale);
	try {
		if (gameLoader.ready())
		{
			if (NetworkHandshake::instance != nullptr)
			{
				networkStatus = NetworkHandshake::instance->start();
				gui_state = GuiState::NetworkStart;
			}
			else
			{
				gui_state = GuiState::Closed;
				ImGui::Text("Starting...");
			}
		}
		else
		{
			const char *label = gameLoader.getProgress().label;
			if (label == nullptr)
				label = "Loading...";
			ImGui::Text("%s", label);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.557f, 0.268f, 0.965f, 1.f));
			ImGui::ProgressBar(gameLoader.getProgress().progress, ImVec2(-1, 20.f * settings.display.uiScale), "");
			ImGui::PopStyleColor();

			float currentwidth = ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX((currentwidth - 100.f * settings.display.uiScale) / 2.f + ImGui::GetStyle().WindowPadding.x);
			ImGui::SetCursorPosY(126.f * settings.display.uiScale);
			if (ImGui::Button("Cancel", ScaledVec2(100.f, 0)))
				gameLoader.cancel();
		}
	} catch (const FlycastException& ex) {
		ERROR_LOG(BOOT, "%s", ex.what());
		gui_error(ex.what());
#ifdef TEST_AUTOMATION
		die("Game load failed");
#endif
		emu.unloadGame();
		gui_state = GuiState::Main;
	}
	ImGui::PopStyleVar();

    ImGui::End();
}

void gui_display_ui()
{
	if (gui_state == GuiState::Closed || gui_state == GuiState::VJoyEdit)
		return;
	if (gui_state == GuiState::Main)
	{
		if (!settings.content.path.empty())
		{
#ifndef __ANDROID__
			commandLineStart = true;
#endif
			gui_start_game(settings.content.path);
			return;
		}
	}

	gui_newFrame();
	ImGui::NewFrame();
	error_msg_shown = false;

	switch (gui_state)
	{
	case GuiState::Settings:
		gui_display_settings();
		break;
	case GuiState::Commands:
		gui_display_commands();
		break;
	case GuiState::Main:
		//gui_display_demo();
		gui_display_content();
		break;
	case GuiState::Closed:
		break;
	case GuiState::Onboarding:
		gui_display_onboarding();
		break;
	case GuiState::VJoyEdit:
		break;
	case GuiState::VJoyEditCommands:
#ifdef __ANDROID__
		gui_display_vjoy_commands();
#endif
		break;
	case GuiState::SelectDisk:
		gui_display_content();
		break;
	case GuiState::Loading:
		gui_display_loadscreen();
		break;
	case GuiState::NetworkStart:
		gui_network_start();
		break;
	case GuiState::Cheats:
		gui_cheats();
		break;
	default:
		die("Unknown UI state");
		break;
	}
	error_popup();
	gui_endFrame();

	if (gui_state == GuiState::Closed)
		emu.start();
}

static float LastFPSTime;
static int lastFrameCount = 0;
static float fps = -1;

static std::string getFPSNotification()
{
	if (config::ShowFPS)
	{
		double now = os_GetSeconds();
		if (now - LastFPSTime >= 1.0) {
			fps = (MainFrameCount - lastFrameCount) / (now - LastFPSTime);
			LastFPSTime = now;
			lastFrameCount = MainFrameCount;
		}
		if (fps >= 0.f && fps < 9999.f) {
			char text[32];
			snprintf(text, sizeof(text), "F:%.1f%s", fps, settings.input.fastForwardMode ? " >>" : "");

			return std::string(text);
		}
	}
	return std::string(settings.input.fastForwardMode ? ">>" : "");
}

void gui_display_osd()
{
	if (gui_state == GuiState::VJoyEdit)
		return;
	std::string message = get_notification();
	if (message.empty())
		message = getFPSNotification();

//	if (!message.empty() || config::FloatVMUs || crosshairsNeeded() || (ggpo::active() && config::NetworkStats))
	{
		gui_newFrame();
		ImGui::NewFrame();

		if (!message.empty())
		{
			ImGui::SetNextWindowBgAlpha(0);
			ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always, ImVec2(0.f, 1.f));	// Lower left corner
			ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0));

			ImGui::Begin("##osd", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
					| ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
			ImGui::SetWindowFontScale(1.5);
			ImGui::TextColored(ImVec4(1, 1, 0, 0.7), "%s", message.c_str());
			ImGui::End();
		}
		imguiDriver->displayCrosshairs();
		if (config::FloatVMUs)
			imguiDriver->displayVmus();
//		gui_plot_render_time(settings.display.width, settings.display.height);
		if (ggpo::active())
		{
			if (config::NetworkStats)
				ggpo::displayStats();
			chat.display();
		}
		lua::overlay();

		gui_endFrame();
	}
}

void gui_open_onboarding()
{
	gui_state = GuiState::Onboarding;
}

void gui_cancel_load()
{
	gameLoader.cancel();
}

void gui_term()
{
	if (inited)
	{
		inited = false;
		ImGui::DestroyContext();
	    EventManager::unlisten(Event::Resume, emuEventCallback);
	    EventManager::unlisten(Event::Start, emuEventCallback);
	    EventManager::unlisten(Event::Terminate, emuEventCallback);
	}
}

void fatal_error(const char* text, ...)
{
    va_list args;

    char temp[2048];
    va_start(args, text);
    vsnprintf(temp, sizeof(temp), text, args);
    va_end(args);
    ERROR_LOG(COMMON, "%s", temp);

    gui_display_notification(temp, 2000);
}

extern bool subfolders_read;

void gui_refresh_files()
{
	scanner.refresh();
	subfolders_read = false;
}

static void reset_vmus()
{
	for (u32 i = 0; i < ARRAY_SIZE(vmu_lcd_status); i++)
		vmu_lcd_status[i] = false;
}

void gui_error(const std::string& what)
{
	error_msg = what;
}

#ifdef TARGET_UWP
// Ugly but a good workaround for MS stupidity
// UWP doesn't allow the UI thread to wait on a thread/task. When an std::future is ready, it is possible
// that the task has not yet completed. Calling std::future::get() at this point will throw an exception
// AND destroy the std::future at the same time, rendering it invalid and discarding the future result.
bool __cdecl Concurrency::details::_Task_impl_base::_IsNonBlockingThread() {
	return false;
}
#endif
