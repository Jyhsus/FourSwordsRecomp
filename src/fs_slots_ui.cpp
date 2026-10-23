#include "fs_slots_ui.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "launcher_theme.h"

#include "fs_save.h"

// The cartridge's own "CHOOSE A FILE" is skipped (--slot), so this screen is
// where files are made and named. The name rules are the game's: 1 to 6
// characters from the "ENTER A NAME" grid (fs_save::valid_name).

namespace fsr {
namespace {

using save::Image;

constexpr int kStay = 3;   // draw(): nothing chosen this frame

ImVec4 color(const LngColor& c) { return ImVec4(c.r, c.g, c.b, c.a); }

void apply_theme(const LauncherTheme& t) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = t.radius_lg;
    style.FrameRounding = t.radius_sm;
    style.PopupRounding = t.radius_lg;
    style.GrabRounding = t.radius_sm;
    style.FramePadding = ImVec2(12.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.WindowPadding = ImVec2(24.0f, 20.0f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = color(t.background);
    c[ImGuiCol_ChildBg] = color(t.panel);
    c[ImGuiCol_PopupBg] = color(t.background2);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
    c[ImGuiCol_Border] = color(t.border);
    c[ImGuiCol_Text] = color(t.text);
    c[ImGuiCol_TextDisabled] = color(t.text_muted);
    c[ImGuiCol_Button] = color(t.control);
    c[ImGuiCol_ButtonHovered] = color(t.control_hovered);
    c[ImGuiCol_ButtonActive] = color(t.accent_dim);
    c[ImGuiCol_FrameBg] = color(t.control);
    c[ImGuiCol_FrameBgHovered] = color(t.control_hovered);
    c[ImGuiCol_FrameBgActive] = color(t.control_hovered);
    c[ImGuiCol_TitleBgActive] = color(t.panel);
    c[ImGuiCol_NavCursor] = color(t.focus_ring);
    c[ImGuiCol_TextSelectedBg] = color(t.accent_dim);
}

std::size_t codepoints(const char* s) {
    std::size_t n = 0;
    for (; *s; ++s) n += (static_cast<unsigned char>(*s) & 0xC0) != 0x80;
    return n;
}

// Cuts a NUL-terminated UTF-8 buffer to at most `max` code points.
void truncate_codepoints(char* s, std::size_t max) {
    std::size_t n = 0;
    for (char* p = s; *p; ++p) {
        if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80 && n++ == max) {
            *p = '\0';
            return;
        }
    }
}

void pop_last_codepoint(char* s) {
    std::size_t len = std::strlen(s);
    while (len > 0 && (static_cast<unsigned char>(s[len - 1]) & 0xC0) == 0x80) --len;
    if (len > 0) s[len - 1] = '\0';
}

// Typed characters that are not on the grid never reach the buffer.
int grid_filter(ImGuiInputTextCallbackData* data) {
    // Every grid character is below U+0800: one or two UTF-8 bytes.
    const unsigned c = data->EventChar;
    if (c >= 0x800) return 1;
    const char utf8[3] = {
        static_cast<char>(c < 0x80 ? c : 0xC0 | (c >> 6)),
        static_cast<char>(c < 0x80 ? 0 : 0x80 | (c & 0x3F)), 0};
    return save::valid_name(utf8) ? 0 : 1;
}

std::string sdl_base_path() {
    char* base = SDL_GetBasePath();
    std::string out = base ? base : "";
    SDL_free(base);
    return out;
}

class Screen {
public:
    explicit Screen(const SlotScreen& s) : paths_(s) { reload(); }

    // One frame of UI; returns a slot or a kSlotScreen code once chosen, else kStay.
    int draw(const ImFont* title_font);

    const LocalPlan& local() const { return local_; }

private:
    void reload();
    bool edit(bool (Image::*op)(int, std::string_view, std::string*), int slot,
              const std::string& name);
    bool erase(int slot);
    bool store(Image& image);
    void name_popup();
    void erase_popup();
    int multiplayer_popup();
    void default_local_plan();

    SlotScreen paths_;
    std::optional<Image> image_;   // empty when there is no .sav yet
    bool broken_ = false;          // a .sav that fs_save cannot read
    std::array<save::Slot, save::kSlotCount> slots_{};
    std::string error_;

    int target_ = -1;              // slot the open popup acts on
    bool creating_ = false;
    char name_[32] = {};

    enum class Page { Choose, Local, Online };
    Page page_ = Page::Choose;     // of the Multiplayer popup
    LocalPlan local_;
};

void Screen::reload() {
    std::error_code ec;
    broken_ = false;
    image_.reset();
    slots_ = {};
    if (!std::filesystem::exists(paths_.save_path, ec)) return;
    std::string error;
    image_ = Image::load(paths_.save_path, &error);
    if (!image_) {
        broken_ = true;
        error_ = error;
        return;
    }
    slots_ = image_->slots();
}

bool Screen::store(Image& image) {
    std::error_code ec;
    if (paths_.save_path.has_parent_path()) {
        std::filesystem::create_directories(paths_.save_path.parent_path(), ec);
    }
    std::string error;
    if (!image.store(paths_.save_path, &error)) {
        error_ = error;
        return false;
    }
    error_.clear();
    reload();
    return true;
}

bool Screen::edit(bool (Image::*op)(int, std::string_view, std::string*), int slot,
                  const std::string& name) {
    std::optional<Image> working = image_;
    if (!working) {
        // First file on a new save: start from what the game writes on its
        // first boot, built from the player's ROM.
        std::ifstream in(paths_.rom_path, std::ios::binary);
        std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
        working = Image::blank(rom, &error_);
        if (!working) return false;
    }
    if (!((*working).*op)(slot, name, &error_)) return false;
    return store(*working);
}

bool Screen::erase(int slot) {
    if (!image_) return false;
    Image working = *image_;
    if (!working.erase(slot, &error_)) return false;
    return store(working);
}

void Screen::name_popup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Name", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        return;
    }
    ImGui::Text(creating_ ? "Name for file %d" : "New name for file %d", target_ + 1);
    ImGui::TextDisabled("Up to %zu characters from the game's name grid.", save::kNameMax);

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(260.0f);
    bool submit = ImGui::InputText("##name", name_, sizeof(name_),
                                   ImGuiInputTextFlags_CallbackCharFilter |
                                       ImGuiInputTextFlags_EnterReturnsTrue,
                                   &grid_filter);
    truncate_codepoints(name_, save::kNameMax);

    // The grid, for the accented letters a keyboard may not have.
    const auto& grid = save::name_characters();
    const float cell = ImGui::GetFrameHeight();
    int column = 0;
    for (std::size_t i = 0; i < grid.size(); ++i) {
        if (grid[i] == " ") continue;  // the Space button below
        if (column++ % 16 != 0) ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button(grid[i].c_str(), ImVec2(cell, cell)) &&
            codepoints(name_) < save::kNameMax) {
            std::strncat(name_, grid[i].c_str(), sizeof(name_) - std::strlen(name_) - 1);
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Space") && codepoints(name_) < save::kNameMax) {
        std::strncat(name_, " ", sizeof(name_) - std::strlen(name_) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) pop_last_codepoint(name_);

    ImGui::Separator();
    const bool valid = save::valid_name(name_);
    ImGui::BeginDisabled(!valid);
    submit = (ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || submit) && valid;
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (submit) {
        if (edit(creating_ ? &Image::create : &Image::rename, target_, name_)) {
            ImGui::CloseCurrentPopup();
        }
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", error_.c_str());
    ImGui::EndPopup();
}

void Screen::erase_popup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Erase", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        return;
    }
    ImGui::Text("Erase file %d, \"%s\"?", target_ + 1, slots_[target_].name.c_str());
    ImGui::TextDisabled("The cartridge shares its files with A Link to the Past:\n"
                        "that game's progress in this file is erased too.");
    ImGui::Separator();
    if (ImGui::Button("Erase", ImVec2(120.0f, 0.0f))) {
        if (erase(target_)) ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }
    if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", error_.c_str());
    ImGui::EndPopup();
}

// Player N gets the N-th file in use; the others play as guests.
void Screen::default_local_plan() {
    local_.save_path = paths_.save_path;
    local_.file.fill(kGuestFile);
    int player = 0;
    for (int i = 0; i < save::kSlotCount && player < kLocalMaxPlayers; ++i) {
        if (slots_[i].used) local_.file[player++] = i;
    }
}

int Screen::multiplayer_popup() {
    int result = kStay;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Multiplayer", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        return result;
    }
    const ImVec2 wide(240.0f, 0.0f);
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    if (page_ == Page::Choose) {
        if (ImGui::Button("Local", wide)) {
            default_local_plan();
            page_ = Page::Local;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("2 to 4 players on this computer, split screen");
        if (ImGui::Button("Online", wide)) page_ = Page::Online;
        ImGui::SameLine();
        ImGui::TextDisabled("2 to 4 players over the internet or a LAN");
        ImGui::Separator();
        close = ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || close;
    } else if (page_ == Page::Local) {
        ImGui::TextUnformatted("Players");
        for (int n = 2; n <= kLocalMaxPlayers; ++n) {
            ImGui::SameLine();
            ImGui::RadioButton(std::to_string(n).c_str(), &local_.players, n);
        }
        for (int p = 0; p < local_.players; ++p) {
            ImGui::PushID(p);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Player %d", p + 1);
            ImGui::SameLine(110.0f);
            auto label = [this](int file) {
                return file == kGuestFile ? std::string("Guest (not saved)")
                                          : "File " + std::to_string(file + 1) + "  " + slots_[file].name;
            };
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("##file", label(local_.file[p]).c_str())) {
                for (int file = kGuestFile; file < save::kSlotCount; ++file) {
                    if (file != kGuestFile && !slots_[file].used) continue;
                    bool taken = false;
                    for (int q = 0; q < local_.players; ++q) {
                        taken |= q != p && file != kGuestFile && local_.file[q] == file;
                    }
                    ImGui::BeginDisabled(taken);
                    if (ImGui::Selectable(label(file).c_str(), local_.file[p] == file)) local_.file[p] = file;
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextDisabled(p == 0 ? "keyboard + controller 1" : "controller %d", p + 1);
            ImGui::PopID();
        }
        const int pads = SDL_NumJoysticks();
        if (pads < local_.players) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                               "%d controller(s) connected; players without one cannot move.", pads);
        }
        ImGui::TextDisabled("Each file is saved back on its own; a guest's progress is not kept.");
        ImGui::Separator();
        // Every file is played by one GBA at most (its blocks are merged back).
        ImGui::BeginDisabled(!valid_local_plan(local_, slots_));
        if (ImGui::Button("Start", ImVec2(120.0f, 0.0f))) result = kSlotScreenLocal;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Back", ImVec2(120.0f, 0.0f))) page_ = Page::Choose;
    } else {
        ImGui::TextUnformatted("Online rooms are on the launcher's netplay page.");
        ImGui::TextDisabled("Every player needs their own copy of the game and BIOS.");
        ImGui::Separator();
        if (ImGui::Button("Open the launcher", ImVec2(200.0f, 0.0f))) result = kSlotScreenOnline;
        ImGui::SameLine();
        if (ImGui::Button("Back", ImVec2(120.0f, 0.0f))) page_ = Page::Choose;
    }
    if (close || result != kStay) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return result;
}

int Screen::draw(const ImFont* title_font) {
    int result = kStay;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##files", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushFont(const_cast<ImFont*>(title_font));
    ImGui::TextUnformatted("Four Swords");
    ImGui::PopFont();
    ImGui::TextDisabled("Choose a file");
    ImGui::Spacing();

    const char* open_popup = nullptr;
    const float button_w = 110.0f;
    const float row_h = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    for (int i = 0; i < save::kSlotCount; ++i) {
        ImGui::PushID(i);
        ImGui::BeginChild("slot", ImVec2(0.0f, row_h), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("FILE %d", i + 1);
        ImGui::SameLine(90.0f);
        if (slots_[i].used) {
            ImGui::TextUnformatted(slots_[i].name.c_str());
        } else {
            ImGui::TextDisabled(broken_ ? "-" : "Empty");
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const int buttons = slots_[i].used ? 3 : 1;
        ImGui::SameLine(ImGui::GetContentRegionMax().x -
                        buttons * button_w - (buttons - 1) * style.ItemSpacing.x);
        ImGui::BeginDisabled(broken_);
        if (slots_[i].used) {
            if (ImGui::Button("Play", ImVec2(button_w, 0.0f))) result = i;
            ImGui::SameLine();
            if (ImGui::Button("Rename", ImVec2(button_w, 0.0f))) {
                std::snprintf(name_, sizeof(name_), "%s", slots_[i].name.c_str());
                creating_ = false;
                target_ = i;
                open_popup = "Name";
            }
            ImGui::SameLine();
            if (ImGui::Button("Erase", ImVec2(button_w, 0.0f))) {
                target_ = i;
                open_popup = "Erase";
            }
        } else if (ImGui::Button("New file", ImVec2(button_w, 0.0f))) {
            name_[0] = '\0';
            creating_ = true;
            target_ = i;
            open_popup = "Name";
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(button_w, 0.0f)) ||
        (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
         ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        result = kSlotScreenBack;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(broken_);
    if (ImGui::Button("Multiplayer", ImVec2(button_w * 1.4f, 0.0f))) {
        page_ = Page::Choose;
        open_popup = "Multiplayer";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", paths_.save_path.filename().string().c_str());
    if (!error_.empty() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", error_.c_str());
    }

    if (open_popup) {
        error_.clear();
        ImGui::OpenPopup(open_popup);
    }
    name_popup();
    erase_popup();
    if (const int chosen = multiplayer_popup(); chosen != kStay) result = chosen;
    ImGui::End();
    return result;
}

}  // namespace

int run_slot_screen(SlotScreen& paths) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "[fsr] file screen: %s\n", SDL_GetError());
        return kSlotScreenQuit;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow(
        "FourSwordsRecomp \xE2\x80\x94 Files", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        880, 640, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!gl) {
        std::fprintf(stderr, "[fsr] file screen: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_GL_ResetAttributes();
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return kSlotScreenQuit;
    }
    SDL_SetWindowMinimumSize(window, 800, 600);  // the name dialog fits
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    const LauncherTheme theme = launcher_theme_gba();
    apply_theme(theme);

    // Latin-1 plus the Œ œ ligatures and the em dash: every grid character.
    static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x0152, 0x0153, 0x2014, 0x2014, 0};
    const std::string font = sdl_base_path() + "assets/fonts/LatoLatin-Regular.ttf";
    const std::string bold = sdl_base_path() + "assets/fonts/LatoLatin-Bold.ttf";
    ImFont* title_font = nullptr;
    if (std::ifstream(font).good() && std::ifstream(bold).good()) {
        io.Fonts->AddFontFromFileTTF(font.c_str(), 20.0f, nullptr, kRanges);
        title_font = io.Fonts->AddFontFromFileTTF(bold.c_str(), 34.0f, nullptr, kRanges);
    } else {
        // Without the launcher's fonts the accented letters show as '?'.
        std::fprintf(stderr, "[fsr] file screen: %s missing\n", font.c_str());
        io.Fonts->AddFontDefault();
        title_font = io.Fonts->Fonts[0];
    }

    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    Screen screen(paths);
    int result = kStay;
    while (result == kStay) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                 ev.window.windowID == SDL_GetWindowID(window))) {
                result = kSlotScreenQuit;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const int picked = screen.draw(title_font);
        if (result == kStay) result = picked;
        ImGui::Render();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(theme.background.r, theme.background.g, theme.background.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    if (result == kSlotScreenLocal) paths.local = screen.local();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_GL_ResetAttributes();
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    return result;
}

}  // namespace fsr
