// =====================================================================
//  RetroConsole - Frontend de galeria de juegos (C++ / SDL2)
//  Sustituye al frontend Python+Pygame del plan original.
//  Compilado por Buildroot como paquete propio: retroconsole-frontend.
//
//  Funcion:
//   - Corre sobre el FRAMEBUFFER de la Pi 4 con SDL2+KMS/DRM (sin X11).
//   - Lee el gamepad DualSense via SDL_GameController.
//   - Lista las ROMs (.nes/.smc/.sfc/.gba) presentes en ROMS_DIR.
//   - Lanza Mednafen al pulsar el boton de confirmar.
//   - Coordina con el daemon de USB por archivos de bandera:
//        PAUSE_FLAG  -> muestra overlay "Importando..."
//        REFRESH_FLAG -> recarga la lista de juegos
//
//  Cero dependencias de Python o pygame. Solo SDL2 + SDL2_ttf.
//  Compatible con la misma estructura de overlay y configuracion.
// =====================================================================

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// =====================================================================
//  Configuracion: se lee de /etc/retroconsole/retroconsole.conf
//  Si no existe, se usan defaults razonables.
// =====================================================================
struct Config {
    std::string roms_dir       = "/home/pi/roms";
    std::string pause_flag     = "/tmp/retro_pause.flag";
    std::string refresh_flag   = "/tmp/retro_refresh.flag";
    std::string emulator_bin   = "/usr/bin/mednafen";
    std::string alsa_device    = "default";

    std::string font_path      = "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf";
    std::string font_path_alt  = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
};

static Config load_config(const std::string& path) {
    Config c;
    std::ifstream f(path);
    if (!f.is_open()) {
        (void)0;
        return c;
    }
    std::string line;
    while (std::getline(f, line)) {
        // quita comentarios y blancos
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);
        // trim
        while (!k.empty() && isspace((unsigned char)k.back())) k.pop_back();
        while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
        size_t i = 0;
        while (i < v.size() && isspace((unsigned char)v[i])) i++;
        v = v.substr(i);

        if      (k == "ROMS_DIR")     c.roms_dir     = v;
        else if (k == "PAUSE_FLAG")   c.pause_flag   = v;
        else if (k == "REFRESH_FLAG") c.refresh_flag = v;
        else if (k == "EMULATOR_BIN") c.emulator_bin = v;
        else if (k == "ALSA_DEVICE")  c.alsa_device  = v;
    }
    return c;
}

// =====================================================================
//  Modelo de datos: una ROM
// =====================================================================
struct Game {
    std::string name;    // sin extension
    std::string path;    // ruta absoluta
    std::string system;  // "NES", "SNES", "GBA"
    bool is_new = false; // ROM recien importada desde USB
};

static std::string to_lower(std::string s) {
    for (auto& c : s) c = tolower((unsigned char)c);
    return s;
}

static const std::map<std::string, std::string> EXT_SYS = {
    {".nes", "NES"}, {".fds", "NES"}, {".unf", "NES"},
    {".smc", "SNES"}, {".sfc", "SNES"}, {".swc", "SNES"}, {".fig", "SNES"},
    {".gba", "GBA"}, {".agb", "GBA"},
};

static void scan_dir_recursive(const std::string& dir, std::vector<Game>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dir + "/" + ent->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir_recursive(full, out);
        } else if (S_ISREG(st.st_mode)) {
            std::string nm(ent->d_name);
            auto dot = nm.find_last_of('.');
            if (dot == std::string::npos) continue;
            std::string ext = to_lower(nm.substr(dot));
            auto it = EXT_SYS.find(ext);
            if (it == EXT_SYS.end()) continue;
            Game g;
            g.name = nm.substr(0, dot);
            g.path = full;
            g.system = it->second;
            // Si existe <path>.new -> marcar como nuevo
            struct stat st_new;
            std::string marker = full + ".new";
            if (stat(marker.c_str(), &st_new) == 0) {
                g.is_new = true;
            }
            out.push_back(g);
        }
    }
    closedir(d);
}

static std::vector<Game> scan_roms(const std::string& dir) {
    std::vector<Game> games;
    scan_dir_recursive(dir, games);
    std::sort(games.begin(), games.end(), [](const Game& a, const Game& b){
        if (a.system != b.system) return a.system < b.system;
        return to_lower(a.name) < to_lower(b.name);
    });
    return games;
}

// =====================================================================
//  Colores por sistema
// =====================================================================
static SDL_Color color_for(const std::string& sys) {
    if (sys == "NES")  return SDL_Color{220, 60, 60, 255};
    if (sys == "SNES") return SDL_Color{90, 110, 220, 255};
    if (sys == "GBA")  return SDL_Color{140, 90, 200, 255};
    return SDL_Color{120, 120, 120, 255};
}

// =====================================================================
//  Helpers de renderizado
// =====================================================================
struct Renderer {
    SDL_Window*   win = nullptr;
    SDL_Renderer* ren = nullptr;
    TTF_Font*     font_big = nullptr;
    TTF_Font*     font_mid = nullptr;
    TTF_Font*     font_sm  = nullptr;
    int W = 0, H = 0;

    bool init(const Config& cfg) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
            (void)0;
            return false;
        }
        if (TTF_Init() != 0) {
            (void)0;
            return false;
        }
        SDL_ShowCursor(SDL_DISABLE);

        // Pantalla completa al tamaño nativo del display
        win = SDL_CreateWindow("RetroConsole", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, 0, 0,
                               SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (!win) {
            (void)0;
            return false;
        }
        SDL_GetWindowSize(win, &W, &H);

        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED |
                                          SDL_RENDERER_PRESENTVSYNC);
        if (!ren) {
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!ren) {
            (void)0;
            return false;
        }

        // Cargar fuente; probar dos rutas tipicas en Buildroot
        const char* paths[] = { cfg.font_path.c_str(), cfg.font_path_alt.c_str() };
        for (auto p : paths) {
            font_big = TTF_OpenFont(p, std::max(28, H / 16));
            if (font_big) {
                font_mid = TTF_OpenFont(p, std::max(22, H / 24));
                font_sm  = TTF_OpenFont(p, std::max(18, H / 34));
                (void)0;
                break;
            }
        }
        if (!font_big || !font_mid || !font_sm) {
            (void)0;
            return false;
        }
        return true;
    }

    void shutdown() {
        if (font_big) TTF_CloseFont(font_big);
        if (font_mid) TTF_CloseFont(font_mid);
        if (font_sm)  TTF_CloseFont(font_sm);
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
    }

    void clear(SDL_Color c) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
        SDL_RenderClear(ren);
    }

    void fill_rect(int x, int y, int w, int h, SDL_Color c) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
        SDL_Rect r{x, y, w, h};
        SDL_RenderFillRect(ren, &r);
    }

    void draw_text(TTF_Font* f, const std::string& s, int x, int y, SDL_Color c) {
        if (s.empty() || !f) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(surf);
    }

    int text_width(TTF_Font* f, const std::string& s) {
        if (!f || s.empty()) return 0;
        int w = 0, h = 0;
        TTF_SizeUTF8(f, s.c_str(), &w, &h);
        return w;
    }

    void present() { SDL_RenderPresent(ren); }
};

// =====================================================================
//  Lanzamiento del emulador Mednafen
// =====================================================================
static void launch_game(const Config& cfg, const Game& g, Renderer& R) {
    (void)0;

    // Liberamos SDL/framebuffer para que Mednafen lo tome
    R.shutdown();

    pid_t pid = fork();
    if (pid == 0) {
        // hijo: ejecuta mednafen
        std::string sd = "ALSA:" + cfg.alsa_device;
        if (cfg.alsa_device == "default") sd = "sexyal-literal-default";
        execl(cfg.emulator_bin.c_str(), "mednafen", "-force_module", (g.system == "SNES" ? "snes_faust" : (g.system == "NES" ? "nes" : "gba")),
              "-sound.device", sd.c_str(),
              "-video.fs", "1",
              g.path.c_str(),
              (char*)nullptr);
        // si execl falla:
        (void)0;
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
    } else {
        (void)0;
    }

    // Reinicializamos SDL al volver del juego
    R.init(cfg);
}

// =====================================================================
//  Flags compartidos con el daemon USB
// =====================================================================
static bool file_exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

static void remove_file(const std::string& p) { unlink(p.c_str()); }

// =====================================================================
//  Aplicacion
// =====================================================================
class Gallery {
    Config cfg;
    Renderer R;
    std::vector<Game> games;
    std::vector<Game> filtered;
    int cursor = 0;
    std::string filter = "ALL";
    std::vector<std::string> filters = {"ALL", "NES", "SNES", "GBA"};
    SDL_GameController* pad = nullptr;
    int joy_id = -1;
    bool paused = false;
    bool running = true;

public:
    Gallery(const Config& c) : cfg(c) {}

    bool init() {
        if (!R.init(cfg)) return false;
        scan();
        // Abrir el primer gamepad disponible
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                if (pad) {
                    joy_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
                    (void)0;
                    break;
                }
            }
        }
        if (!pad) {
            (void)0;
        }
        return true;
    }

    void shutdown() {
        if (pad) SDL_GameControllerClose(pad);
        R.shutdown();
    }

    void scan() {
        games = scan_roms(cfg.roms_dir);
        apply_filter();
        (void)0;
    }

    void apply_filter() {
        filtered.clear();
        for (const auto& g : games) {
            if (filter == "ALL" || g.system == filter)
                filtered.push_back(g);
        }
        if (cursor >= (int)filtered.size())
            cursor = std::max(0, (int)filtered.size() - 1);
    }

    void cycle_system() {
        auto it = std::find(filters.begin(), filters.end(), filter);
        size_t idx = (it == filters.end()) ? 0 : (size_t)(it - filters.begin());
        idx = (idx + 1) % filters.size();
        filter = filters[idx];
        cursor = 0;
        apply_filter();
    }

    void move(int d) {
        if (filtered.empty()) return;
        int n = (int)filtered.size();
        cursor = ((cursor + d) % n + n) % n;
        // Al posicionar cursor en juego nuevo: borrar marcador
        Game& g = filtered[cursor];
        if (g.is_new) {
            std::string marker = g.path + ".new";
            unlink(marker.c_str());
            g.is_new = false;
            for (auto& gg : games) {
                if (gg.path == g.path) { gg.is_new = false; break; }
            }
        }
    }

    void draw() {
        R.clear(SDL_Color{18, 18, 24, 255});

        // Titulo
        R.draw_text(R.font_big, "RETRO CONSOLE", 40, 24,
                    SDL_Color{240, 240, 250, 255});

        // Linea de filtro/count
        int yh = 24 + TTF_FontHeight(R.font_big);
        char tmp[160];
        snprintf(tmp, sizeof(tmp), "Filtro: %s   (%zu juegos)",
                 filter.c_str(), filtered.size());
        R.draw_text(R.font_mid, tmp, 40, yh, SDL_Color{170, 170, 190, 255});

        // Lista
        int list_top = yh + TTF_FontHeight(R.font_mid) + 20;
        int row_h    = TTF_FontHeight(R.font_mid) + 14;
        int visible  = std::max(1, (R.H - list_top - 60) / row_h);
        int start    = std::max(0, cursor - visible / 2);
        int end      = std::min((int)filtered.size(), start + visible);

        if (filtered.empty()) {
            R.draw_text(R.font_mid,
                "No hay ROMs. Inserta un USB con juegos para importarlos.",
                40, list_top, SDL_Color{200, 160, 120, 255});
        } else {
            int y = list_top;
            for (int i = start; i < end; i++) {
                const Game& g = filtered[i];
                bool selected = (i == cursor);
                if (selected) {
                    R.fill_rect(30, y - 4, R.W - 60, row_h,
                                SDL_Color{45, 50, 70, 255});
                }
                // etiqueta de sistema
                SDL_Color sc = color_for(g.system);
                R.fill_rect(40, y + 2, 70, row_h - 12, sc);
                int tw = R.text_width(R.font_sm, g.system);
                R.draw_text(R.font_sm, g.system, 40 + (70 - tw) / 2, y + 6,
                            SDL_Color{255, 255, 255, 255});
                // nombre
                SDL_Color nc = selected ? SDL_Color{255, 255, 255, 255}
                                        : SDL_Color{200, 200, 210, 255};
                R.draw_text(R.font_mid, g.name, 130, y + 2, nc);
                // Etiqueta NUEVO: oculta cuando esta seleccionado
                if (g.is_new && !selected) {
                    int name_w = R.text_width(R.font_mid, g.name);
                    int badge_x = 130 + name_w + 20;
                    int badge_w = R.text_width(R.font_sm, "NUEVO") + 16;
                    R.fill_rect(badge_x, y + 2, badge_w, row_h - 12,
                                SDL_Color{255, 200, 40, 255});
                    R.draw_text(R.font_sm, "NUEVO", badge_x + 8, y + 6,
                                SDL_Color{40, 40, 40, 255});
                }
                y += row_h;
            }
        }

        // Pie de ayuda
        R.draw_text(R.font_sm,
            "D-Pad: mover   X/Circulo: jugar/atras   L1/R1: pagina   "
            "Triangulo: cambiar sistema   Options: recargar",
            40, R.H - 40, SDL_Color{140, 140, 160, 255});

        // Overlay si el daemon esta importando
        if (paused) draw_overlay("Importando ROMs desde USB... Por favor espera");

        R.present();
    }

    void draw_overlay(const std::string& msg) {
        SDL_SetRenderDrawBlendMode(R.ren, SDL_BLENDMODE_BLEND);
        R.fill_rect(0, 0, R.W, R.H, SDL_Color{0, 0, 0, 190});
        int tw = R.text_width(R.font_mid, msg);
        int th = TTF_FontHeight(R.font_mid);
        R.draw_text(R.font_mid, msg, (R.W - tw) / 2, (R.H - th) / 2,
                    SDL_Color{255, 230, 160, 255});
    }

    void handle_button(SDL_GameControllerButton b) {
        if (paused) return;
        switch (b) {
            case SDL_CONTROLLER_BUTTON_A: // X en DualSense
                do_launch();
                break;
            case SDL_CONTROLLER_BUTTON_Y: // Triangulo
                cycle_system();
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  // L1
                move(-5);
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: // R1
                move(5);
                break;
            case SDL_CONTROLLER_BUTTON_START:         // Options
                scan();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                move(-1);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                move(1);
                break;
            default:
                break;
        }
    }

    void do_launch() {
        if (filtered.empty()) return;
        Game g = filtered[cursor];
        // 1) Cerrar pad ANTES de Mednafen
        if (pad) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
            joy_id = -1;
        }
        // 2) Lanzar Mednafen
        launch_game(cfg, g, R);
        // 3) Reabrir SDL Joystick subsystem
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
        SDL_Delay(300);
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
        SDL_GameControllerEventState(SDL_ENABLE);
        // 4) Escanear y reabrir gamepad
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                if (pad) {
                    joy_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
                    break;
                }
            }
        }
        // 5) Vaciar cola de eventos
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    }

    void check_flags() {
        paused = file_exists(cfg.pause_flag);
        if (file_exists(cfg.refresh_flag)) {
            remove_file(cfg.refresh_flag);
            scan();
        }
    }

    void run() {
        Uint32 last_axis = 0;
        while (running) {
            check_flags();

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                    else if (e.key.keysym.sym == SDLK_RETURN) do_launch();
                    else if (e.key.keysym.sym == SDLK_UP) move(-1);
                    else if (e.key.keysym.sym == SDLK_DOWN) move(1);
                    else if (e.key.keysym.sym == SDLK_TAB) cycle_system();
                }
                else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                    handle_button((SDL_GameControllerButton)e.cbutton.button);
                }
                else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                    if (!pad) {
                        pad = SDL_GameControllerOpen(e.cdevice.which);
                        if (pad) (void)0;
                    }
                }
                else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                    if (pad && e.cdevice.which == joy_id) {
                        SDL_GameControllerClose(pad);
                        pad = nullptr;
                        (void)0;
                    }
                }
            }

            // Navegacion analogica con cooldown
            if (pad && !paused) {
                Uint32 now = SDL_GetTicks();
                if (now - last_axis > 50) {
                    Sint16 v = SDL_GameControllerGetAxis(pad,
                        SDL_CONTROLLER_AXIS_LEFTY);
                    if (v < -10000) { move(-1); last_axis = now; }
                    else if (v > 10000) { move(1); last_axis = now; }
                }
            }

            draw();
            SDL_Delay(16); // ~30 fps
        }
    }
};

int main(int /*argc*/, char** /*argv*/) {
    // Permite que el usuario espere unos ms a que el framebuffer este listo
    usleep(800 * 1000);

    Config cfg = load_config("/etc/retroconsole/retroconsole.conf");

    Gallery app(cfg);
    if (!app.init()) {
        (void)0;
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
