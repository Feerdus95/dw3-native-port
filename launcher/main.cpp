/* The launcher, visual prototype: Play, a Settings section and an extraction
   error, with demo values. It reads and writes nothing: no extraction, no
   native.ini, no network. For judging the look and the navigation.

   dw3launcher [--lang N] [--state ready|nogame|error] [--page play|settings]
               [--dpi X] [--size WxH] [--keys down,enter,pad-down,pad-a,...] [--shot out.png]
   --shot draws in a hidden window, plays --keys, saves the picture and exits. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

enum { LANGS = 5 };
enum {
    S_NAME, S_PLAY_PAGE, S_SETTINGS_PAGE, S_LANGUAGE, S_READY_EYEBROW, S_READY_TITLE, S_READY_BODY, S_PORT, S_DATA_FOUND, S_DATA_MISSING,
    S_PLAY, S_OPEN_FOLDER, S_UPDATE, S_UPDATE_OPEN, S_SETUP_EYEBROW, S_SETUP_TITLE, S_SETUP_BODY, S_DROP, S_BROWSE, S_ISO, S_ERR_TITLE,
    S_ERR_WHAT, S_ERR_FIX, S_ERR_RETRY, S_ERR_LOG, S_ST_READY, S_ST_SETUP, S_ST_ERROR, S_SET_TITLE, S_SET_NOTE, S_VIDEO, S_FULLSCREEN,
    S_RES, S_FPS, S_VSYNC, S_WIDE, S_RESET, S_FAN, S_COUNT
};
static const char *const T[LANGS][S_COUNT] = {
    {"English", "Play", "Settings", "Language", "DIGIMON WORLD 2003", "Ready to play", "Your disc has been read, and the port is ready on this PC.",
     "Port", "Game data found", "No game data", "PLAY", "Open folder", "A new version is available: v0.17.0", "Open download page",
     "FIRST TIME, ONLY ONCE", "Set up your game", "The port plays from your own copy of Digimon World 2003 (Europe, SLES-03936). It's read once into the data folder, about 850 MB.",
     "Drop your .cue or .bin here", "Browse...", "An .iso may lose the movies' sound: a .cue/.bin is best.", "Your disc image is incomplete",
     "The image ends before the game's last file, so nothing was installed.", "Copy or dump the image again, then choose it here. Your data folder wasn't changed.",
     "Choose another image", "Open the log", "All set", "Game not set up yet", "Setup failed", "Settings",
     "Prototype: these values are examples and aren't saved.", "Video", "Fullscreen", "Internal resolution", "Frame rate", "VSync", "Widescreen",
     "Reset to defaults", "Fan project, not affiliated with Bandai Namco."},
    {"Español", "Jugar", "Ajustes", "Idioma", "DIGIMON WORLD 2003", "Listo para jugar", "Tu disco ya se leyó y el port está listo en esta PC.",
     "Port", "Datos del juego encontrados", "Sin datos del juego", "JUGAR", "Abrir carpeta", "Hay una versión nueva: v0.17.0", "Abrir la página de descarga",
     "PRIMERA VEZ, UNA SOLA VEZ", "Prepara tu juego", "El port se juega con tu propia copia de Digimon World 2003 (Europa, SLES-03936). Se lee una sola vez a la carpeta data, unos 850 MB.",
     "Arrastra aquí tu .cue o .bin", "Buscar...", "Una .iso puede perder el sonido de los videos: mejor un .cue/.bin.", "La imagen de tu disco está incompleta",
     "La imagen termina antes del último archivo del juego, así que no se instaló nada.", "Vuelve a copiar o extraer la imagen y elígela aquí. Tu carpeta data no cambió.",
     "Elegir otra imagen", "Abrir el registro", "Todo listo", "Juego sin preparar", "Falló la preparación", "Ajustes",
     "Prototipo: estos valores son de ejemplo y no se guardan.", "Video", "Pantalla completa", "Resolución interna", "Fotogramas por segundo", "VSync", "Pantalla ancha",
     "Restablecer valores", "Proyecto de fans, sin relación con Bandai Namco."},
    {"Français", "Jouer", "Options", "Langue", "DIGIMON WORLD 2003", "Prêt à jouer", "Votre disque a été lu et le portage est prêt sur ce PC.",
     "Portage", "Données du jeu trouvées", "Aucune donnée du jeu", "JOUER", "Ouvrir le dossier", "Une nouvelle version est disponible : v0.17.0", "Ouvrir la page de téléchargement",
     "PREMIÈRE FOIS, UNE SEULE FOIS", "Préparez votre jeu", "Le portage se joue avec votre propre copie de Digimon World 2003 (Europe, SLES-03936). Elle est lue une seule fois dans le dossier data, environ 850 Mo.",
     "Déposez votre .cue ou .bin ici", "Parcourir...", "Un .iso peut perdre le son des vidéos : préférez un .cue/.bin.", "L'image de votre disque est incomplète",
     "L'image s'arrête avant le dernier fichier du jeu : rien n'a été installé.", "Copiez ou extrayez de nouveau l'image, puis choisissez-la ici. Votre dossier data n'a pas changé.",
     "Choisir une autre image", "Ouvrir le journal", "Tout est prêt", "Jeu non préparé", "Échec de la préparation", "Options",
     "Prototype : ces valeurs sont des exemples et ne sont pas enregistrées.", "Vidéo", "Plein écran", "Résolution interne", "Images par seconde", "VSync", "Écran large",
     "Rétablir les valeurs", "Projet de fans, sans lien avec Bandai Namco."},
    {"Deutsch", "Spielen", "Einstellungen", "Sprache", "DIGIMON WORLD 2003", "Bereit zum Spielen", "Deine Disc wurde gelesen und der Port ist auf diesem PC bereit.",
     "Port", "Spieldaten gefunden", "Keine Spieldaten", "SPIELEN", "Ordner öffnen", "Eine neue Version ist verfügbar: v0.17.0", "Downloadseite öffnen",
     "ERSTES MAL, NUR EINMAL", "Richte dein Spiel ein", "Der Port läuft mit deiner eigenen Kopie von Digimon World 2003 (Europa, SLES-03936). Sie wird einmalig in den Ordner data gelesen, ca. 850 MB.",
     "Zieh deine .cue oder .bin hierher", "Durchsuchen...", "Bei einer .iso kann der Filmton fehlen: besser .cue/.bin.", "Dein Disc-Image ist unvollständig",
     "Das Image endet vor der letzten Datei des Spiels, daher wurde nichts installiert.", "Kopiere oder erstelle das Image neu und wähle es hier aus. Dein Ordner data wurde nicht verändert.",
     "Anderes Image wählen", "Protokoll öffnen", "Alles bereit", "Spiel nicht eingerichtet", "Einrichtung fehlgeschlagen", "Einstellungen",
     "Prototyp: Diese Werte sind Beispiele und werden nicht gespeichert.", "Video", "Vollbild", "Interne Auflösung", "Bildrate", "VSync", "Breitbild",
     "Zurücksetzen", "Fanprojekt, nicht mit Bandai Namco verbunden."},
    {"Italiano", "Gioca", "Impostazioni", "Lingua", "DIGIMON WORLD 2003", "Pronto per giocare", "Il tuo disco è stato letto e il port è pronto su questo PC.",
     "Port", "Dati del gioco trovati", "Nessun dato del gioco", "GIOCA", "Apri la cartella", "È disponibile una nuova versione: v0.17.0", "Apri la pagina di download",
     "PRIMA VOLTA, UNA SOLA VOLTA", "Prepara il tuo gioco", "Il port usa la tua copia di Digimon World 2003 (Europa, SLES-03936). Viene letta una sola volta nella cartella data, circa 850 MB.",
     "Trascina qui il tuo .cue o .bin", "Sfoglia...", "Una .iso può perdere l'audio dei filmati: meglio un .cue/.bin.", "L'immagine del disco è incompleta",
     "L'immagine finisce prima dell'ultimo file del gioco, quindi non è stato installato nulla.", "Copia o estrai di nuovo l'immagine e sceglila qui. La cartella data non è stata modificata.",
     "Scegli un'altra immagine", "Apri il registro", "Tutto pronto", "Gioco non preparato", "Preparazione non riuscita", "Impostazioni",
     "Prototipo: questi valori sono esempi e non vengono salvati.", "Video", "Schermo intero", "Risoluzione interna", "Fotogrammi al secondo", "VSync", "Schermo panoramico",
     "Ripristina i valori", "Progetto di fan, non affiliato a Bandai Namco."},
};
enum { ST_READY, ST_NOGAME, ST_ERROR };
enum { PAGE_PLAY, PAGE_SETTINGS };

static const ImU32 C_ORANGE = IM_COL32(245, 166, 35, 255), C_ORANGE_HI = IM_COL32(255, 190, 80, 255), C_INK = IM_COL32(11, 18, 32, 255),
                   C_TEXT = IM_COL32(232, 236, 244, 255), C_SOFT = IM_COL32(170, 184, 208, 255), C_MUTED = IM_COL32(120, 136, 166, 255),
                   C_GREEN = IM_COL32(74, 214, 132, 255), C_RED = IM_COL32(255, 107, 107, 255), C_LINE = IM_COL32(79, 106, 160, 110),
                   C_PANEL = IM_COL32(20, 32, 54, 240), C_BTN = IM_COL32(38, 54, 86, 255), C_BTN_HI = IM_COL32(54, 76, 118, 255);

static float g_s = 1;          /* the screen's scale */
static ImFont *g_body, *g_small, *g_title, *g_big;
static ImGuiStyle g_base;
static int g_lang, g_state = ST_READY, g_page = PAGE_PLAY;
static bool g_focus = true;    /* the page's first control takes the focus on the next frame */
static std::string g_note;     /* what a demo button would have done */
static struct { bool fullscreen = false, vsync = true, wide = false; int res = 3, fps = 0; } g_set;

static float S(float v) { return v * g_s; }
static const char *t(int s) { return T[g_lang][s]; }

/* Everything sized for the scale: on start, and when the window moves to another screen */
static void apply_scale(float scale) {
    ImGuiIO &io = ImGui::GetIO();
    g_s = scale;
    ImGui::GetStyle() = g_base;
    ImGui::GetStyle().ScaleAllSizes(g_s);
    io.Fonts->Clear();
    std::string path = std::string(SDL_GetBasePath()) + "Roboto-Medium.ttf";
    auto font = [&](float size) {
        ImFont *f = io.Fonts->AddFontFromFileTTF(path.c_str(), size * g_s);
        return f ? f : io.Fonts->AddFontDefault();
    };
    g_body = font(17), g_small = font(14), g_title = font(38), g_big = font(23);
    ImGui_ImplSDLRenderer3_DestroyFontsTexture();
    ImGui_ImplSDLRenderer3_CreateFontsTexture();
}

static void set_style() {
    ImGuiStyle &st = ImGui::GetStyle();
    ImVec4 *c = st.Colors;
    st.WindowPadding = ImVec2(0, 0), st.WindowBorderSize = 0, st.ChildBorderSize = 1, st.FrameRounding = 7, st.ChildRounding = 10;
    st.FramePadding = ImVec2(12, 8), st.ItemSpacing = ImVec2(10, 10), st.PopupRounding = 7, st.GrabRounding = 7, st.ScrollbarRounding = 7;
    c[ImGuiCol_Text] = ImColor(C_TEXT), c[ImGuiCol_TextDisabled] = ImColor(C_MUTED), c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0), c[ImGuiCol_Border] = ImColor(C_LINE), c[ImGuiCol_PopupBg] = ImColor(IM_COL32(20, 32, 54, 250));
    c[ImGuiCol_FrameBg] = ImColor(C_BTN), c[ImGuiCol_FrameBgHovered] = ImColor(C_BTN_HI), c[ImGuiCol_FrameBgActive] = ImColor(C_BTN_HI);
    c[ImGuiCol_Button] = ImColor(C_BTN), c[ImGuiCol_ButtonHovered] = ImColor(C_BTN_HI), c[ImGuiCol_ButtonActive] = ImColor(C_BTN_HI);
    c[ImGuiCol_Header] = ImColor(C_BTN), c[ImGuiCol_HeaderHovered] = ImColor(C_BTN_HI), c[ImGuiCol_HeaderActive] = ImColor(C_BTN_HI);
    c[ImGuiCol_CheckMark] = ImColor(C_ORANGE), c[ImGuiCol_NavCursor] = ImVec4(1, 1, 1, 1), c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImColor(C_BTN_HI), c[ImGuiCol_ScrollbarGrabHovered] = ImColor(C_MUTED), c[ImGuiCol_ScrollbarGrabActive] = ImColor(C_ORANGE);
    g_base = st;
}

/* A night sky over a grid running to the horizon: drawn, so there's no picture to ship */
static void background(ImDrawList *dl, ImVec2 size) {
    float hz = size.y * 0.74f, cx = size.x * 0.82f;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, hz), IM_COL32(8, 13, 26, 255), IM_COL32(10, 18, 38, 255), IM_COL32(30, 44, 86, 255), IM_COL32(20, 30, 64, 255));
    dl->AddRectFilledMultiColor(ImVec2(0, hz), size, IM_COL32(12, 18, 36, 255), IM_COL32(16, 24, 46, 255), IM_COL32(6, 10, 20, 255), IM_COL32(6, 10, 20, 255));
    for (int i = 48; i > 0; i--) {
        dl->AddCircleFilled(ImVec2(cx, hz), size.y * 0.011f * i, IM_COL32(245, 150, 50, 2), 72);
    }
    dl->PushClipRect(ImVec2(0, 0), ImVec2(size.x, hz), true);
    dl->AddCircleFilled(ImVec2(cx, hz), size.y * 0.10f, IM_COL32(250, 176, 70, 235), 72);
    dl->PopClipRect();
    for (int i = -26; i <= 26; i++) {
        dl->AddLine(ImVec2(cx, hz), ImVec2(cx + i * size.x * 0.12f, size.y), IM_COL32(90, 140, 230, 44), S(1));
    }
    /* the lines would run together at the horizon: they fade in below it */
    dl->AddRectFilledMultiColor(ImVec2(0, hz), ImVec2(size.x, hz + (size.y - hz) * 0.22f), IM_COL32(14, 22, 42, 255), IM_COL32(14, 22, 42, 255), IM_COL32(14, 22, 42, 0), IM_COL32(14, 22, 42, 0));
    float y = 0.012f;
    while (y < 1) {
        dl->AddLine(ImVec2(0, hz + (size.y - hz) * y), ImVec2(size.x, hz + (size.y - hz) * y), IM_COL32(90, 140, 230, (int)(26 + 44 * y)), S(1));
        y *= 1.55f;
    }
    dl->AddLine(ImVec2(0, hz), ImVec2(size.x, hz), IM_COL32(250, 190, 110, 150), S(1.5f));
    unsigned seed = 77;
    for (int i = 0; i < 90; i++) { /* the same stars every frame */
        seed = seed * 1103515245u + 12345u;
        float sx = size.x * (0.62f + 0.38f * (seed >> 8 & 1023) / 1023.0f); /* on the right, clear of the page's text */
        seed = seed * 1103515245u + 12345u;
        float sy = (seed >> 8 & 1023) / 1023.0f * hz * 0.92f;
        dl->AddCircleFilled(ImVec2(sx, sy), S(i % 7 == 0 ? 1.4f : 0.8f), IM_COL32(210, 225, 255, i % 7 == 0 ? 190 : 110));
    }
}

/* A button, in its role. The focus goes to it if the page has just opened and it's the page's first */
static bool button(const char *label, ImVec2 size, bool primary = false, ImFont *font = nullptr) {
    if (g_focus) {
        ImGui::SetKeyboardFocusHere();
        g_focus = false;
    }
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, C_ORANGE);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C_ORANGE_HI);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, C_ORANGE_HI);
        ImGui::PushStyleColor(ImGuiCol_Text, C_INK);
    }
    if (font) ImGui::PushFont(font);
    bool hit = ImGui::Button(label, size);
    if (font) ImGui::PopFont();
    if (primary) ImGui::PopStyleColor(4);
    return hit;
}

/* A small rounded label: a coloured dot, or none */
static void chip(const char *text, ImU32 dot) {
    ImGui::PushFont(g_small);
    ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(text);
    float dots = dot ? S(16) : 0, w = ts.x + dots + S(24), h = ts.y + S(14);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), C_PANEL, h / 2);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), C_LINE, h / 2);
    if (dot) dl->AddCircleFilled(ImVec2(p.x + S(16), p.y + h / 2), S(4), dot);
    dl->AddText(ImVec2(p.x + S(12) + dots, p.y + S(7)), C_TEXT, text);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopFont();
}

static void heading(const char *eyebrow, const char *title, const char *body, float width) {
    ImGui::PushFont(g_small);
    ImGui::TextColored(ImColor(C_ORANGE), "%s", eyebrow);
    ImGui::PopFont();
    ImGui::PushFont(g_title);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextColored(ImColor(C_SOFT), "%s", body);
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, S(6)));
}

/* A bordered group the keyboard and pad walk through as if it weren't there */
static bool panel(const char *id, float width, ImU32 border = C_LINE) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(16)));
    return ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
}
static void panel_end() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

static void page_ready(float w) {
    char version[64];
    heading(t(S_READY_EYEBROW), t(S_READY_TITLE), t(S_READY_BODY), w);
    SDL_snprintf(version, sizeof version, "%s v0.16.0", t(S_PORT));
    chip(version, 0);
    ImGui::SameLine();
    chip(t(S_DATA_FOUND), C_GREEN);
    ImGui::Dummy(ImVec2(0, S(10)));
    if (button(t(S_PLAY), ImVec2(S(220), S(66)), true, g_big)) g_note = "Prototype: the game would start here.";
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(14));
    if (button(t(S_OPEN_FOLDER), ImVec2(0, S(38)))) g_note = "Prototype: the folder would open here.";
    ImGui::Dummy(ImVec2(0, S(8)));
    panel("update", w);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(t(S_UPDATE));
    /* beside the text if both fit, as a long language's may not */
    float bw = ImGui::CalcTextSize(t(S_UPDATE_OPEN)).x + ImGui::GetStyle().FramePadding.x * 2;
    if (ImGui::CalcTextSize(t(S_UPDATE)).x + ImGui::GetStyle().ItemSpacing.x + bw <= ImGui::GetContentRegionAvail().x) ImGui::SameLine();
    if (button(t(S_UPDATE_OPEN), ImVec2(0, 0))) g_note = "Prototype: the release page would open here.";
    panel_end();
}

static void page_setup(float w) {
    heading(t(S_SETUP_EYEBROW), t(S_SETUP_TITLE), t(S_SETUP_BODY), w);
    chip(t(S_DATA_MISSING), C_ORANGE);
    ImGui::Dummy(ImVec2(0, S(6)));
    panel("drop", w, IM_COL32(245, 166, 35, 140));
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushFont(g_big);
    ImGui::SetCursorPosX((w - ImGui::CalcTextSize(t(S_DROP)).x) / 2);
    ImGui::TextUnformatted(t(S_DROP));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4)));
    float bw = S(200);
    ImGui::SetCursorPosX((w - bw) / 2);
    if (button(t(S_BROWSE), ImVec2(bw, S(44)), true)) g_note = "Prototype: the file dialog would open here.";
    ImGui::Dummy(ImVec2(0, S(6)));
    panel_end();
    ImGui::PushFont(g_small);
    ImGui::TextColored(ImColor(C_MUTED), "%s", t(S_ISO));
    ImGui::PopFont();
}

static void page_error(float w) {
    heading(t(S_SETUP_EYEBROW), t(S_SETUP_TITLE), t(S_SETUP_BODY), w);
    panel("error", w, IM_COL32(255, 107, 107, 170));
    ImGui::PushFont(g_big);
    ImGui::TextColored(ImColor(C_RED), "%s", t(S_ERR_TITLE));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(t(S_ERR_WHAT));
    ImGui::TextColored(ImColor(C_SOFT), "%s", t(S_ERR_FIX));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, S(4)));
    if (button(t(S_ERR_RETRY), ImVec2(0, S(42)), true)) g_note = "Prototype: the file dialog would open here.";
    ImGui::SameLine();
    if (button(t(S_ERR_LOG), ImVec2(0, S(42)))) g_note = "Prototype: logs/launcher.log would open here.";
    panel_end();
}

static void row(const char *label, float col) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(col);
}

static void page_settings(float w) {
    static const char *const FPS[] = {"50", "60"};
    char res[8][32];
    const char *res_list[8];
    float col = 0;

    for (int i = 0; i < 8; i++) {
        SDL_snprintf(res[i], sizeof res[i], "%dx  (%d x %d)", i + 1, 320 * (i + 1), 240 * (i + 1));
        res_list[i] = res[i];
    }
    for (int s : {S_FULLSCREEN, S_RES, S_FPS, S_VSYNC, S_WIDE}) {
        col = SDL_max(col, ImGui::CalcTextSize(t(s)).x);
    }
    col += S(40);
    ImGui::PushFont(g_title);
    ImGui::TextUnformatted(t(S_SET_TITLE));
    ImGui::PopFont();
    ImGui::PushFont(g_small);
    ImGui::TextColored(ImColor(C_ORANGE), "%s", t(S_SET_NOTE));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4)));
    panel("video", w);
    ImGui::PushFont(g_big);
    ImGui::TextUnformatted(t(S_VIDEO));
    ImGui::PopFont();
    row(t(S_FULLSCREEN), col);
    if (g_focus) {
        ImGui::SetKeyboardFocusHere();
        g_focus = false;
    }
    ImGui::Checkbox("##full", &g_set.fullscreen);
    row(t(S_RES), col);
    ImGui::SetNextItemWidth(S(220));
    ImGui::Combo("##res", &g_set.res, res_list, 8);
    row(t(S_FPS), col);
    ImGui::SetNextItemWidth(S(220));
    ImGui::Combo("##fps", &g_set.fps, FPS, 2);
    row(t(S_VSYNC), col);
    ImGui::Checkbox("##vsync", &g_set.vsync);
    row(t(S_WIDE), col);
    ImGui::Checkbox("##wide", &g_set.wide);
    ImGui::Dummy(ImVec2(0, S(2)));
    if (button(t(S_RESET), ImVec2(0, 0))) {
        g_set.fullscreen = false, g_set.vsync = true, g_set.wide = false, g_set.res = 3, g_set.fps = 0;
    }
    panel_end();
}

static void frame(void) {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    float side = S(232), status = S(40);
    static bool popup_before, focus_side;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

    /* The arrows don't cross between the side and the page, so: back goes to the side, shoulders change page.
       An open list takes back for itself, so what counts is whether one was open before this frame */
    if (!popup_before && (pressed(ImGuiKey_Escape) || pressed(ImGuiKey_GamepadFaceRight))) focus_side = true;
    if (!popup_before && (pressed(ImGuiKey_GamepadL1) || pressed(ImGuiKey_GamepadR1) || pressed(ImGuiKey_PageUp) || pressed(ImGuiKey_PageDown))) {
        g_page = !g_page;
        g_focus = true;
    }
    background(ImGui::GetBackgroundDrawList(), vp->Size);
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    /* the side: the name, the pages, the language */
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(8, 13, 26, 205));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(20)));
    ImGui::BeginChild("side", ImVec2(side, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(g_big);
    ImGui::TextColored(ImColor(C_ORANGE), "DIGIMON");
    ImGui::TextUnformatted("WORLD 2003");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(14)));
    for (int i = 0; i < 2; i++) {
        bool on = g_page == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? C_BTN : IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
        if (on && focus_side) {
            ImGui::SetKeyboardFocusHere();
            focus_side = false;
        }
        if (ImGui::Button(t(i == 0 ? S_PLAY_PAGE : S_SETTINGS_PAGE), ImVec2(-1, S(42)))) { /* the page it's on: goes into it */
            g_page = i;
            g_focus = true;
        }
        if (on) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + S(4), b.y), C_ORANGE, S(2));
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    if (ImGui::IsWindowFocused() && (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight))) g_focus = true;
    float low = ImGui::GetWindowHeight() - S(164);
    if (ImGui::GetCursorPosY() < low) ImGui::SetCursorPosY(low);
    ImGui::PushFont(g_small);
    ImGui::TextColored(ImColor(C_MUTED), "%s", t(S_LANGUAGE));
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##lang", t(S_NAME))) {
        for (int i = 0; i < LANGS; i++) {
            if (ImGui::Selectable(T[i][S_NAME], i == g_lang)) g_lang = i;
        }
        ImGui::EndCombo();
    }
    static const char *const STATES[] = {"Ready", "No game", "Extraction error"};
    ImGui::PushFont(g_small);
    ImGui::TextColored(ImColor(C_ORANGE), "Prototype: state");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##state", &g_state, STATES, 3)) {
        g_page = PAGE_PLAY;
        g_note.clear();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 0);

    /* the page, then the status line */
    ImGui::BeginChild("main", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(44), S(40)));
    ImGui::BeginChild("page", ImVec2(0, -status), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    ImGui::PopStyleVar();
    float w = SDL_min(ImGui::GetContentRegionAvail().x, S(640));
    if (g_page == PAGE_SETTINGS) page_settings(w);
    else if (g_state == ST_READY) page_ready(w);
    else if (g_state == ST_NOGAME) page_setup(w);
    else page_error(w);
    ImGui::EndChild();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowSize(), p = ImVec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + a.y - status);
    dl->AddRectFilled(p, ImVec2(p.x + a.x, p.y + status), IM_COL32(8, 13, 26, 225));
    dl->AddLine(p, ImVec2(p.x + a.x, p.y), C_LINE);
    ImU32 dot = g_state == ST_READY ? C_GREEN : g_state == ST_NOGAME ? C_ORANGE : C_RED;
    dl->AddCircleFilled(ImVec2(p.x + S(22), p.y + status / 2), S(5), dot);
    ImGui::PushFont(g_small);
    const char *line = g_note.empty() ? t(g_state == ST_READY ? S_ST_READY : g_state == ST_NOGAME ? S_ST_SETUP : S_ST_ERROR) : g_note.c_str();
    float th = ImGui::GetTextLineHeight();
    dl->AddText(ImVec2(p.x + S(36), p.y + (status - th) / 2), C_SOFT, line);
    ImVec2 fan = ImGui::CalcTextSize(t(S_FAN));
    dl->AddText(ImVec2(p.x + a.x - fan.x - S(16), p.y + (status - th) / 2), C_MUTED, t(S_FAN));
    ImGui::PopFont();
    ImGui::EndChild();
    popup_before = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
    ImGui::End();
}

/* the system's language, if it is one of the five */
static int system_lang(void) {
    static const char *const CODES[LANGS] = {"en", "es", "fr", "de", "it"};
    int n, lang = -1;
    SDL_Locale **l = SDL_GetPreferredLocales(&n);

    for (int i = 0; l != NULL && lang < 0 && i < n; i++) {
        for (int k = 0; k < LANGS; k++) {
            if (SDL_strcasecmp(l[i]->language, CODES[k]) == 0) lang = k;
        }
    }
    SDL_free(l);
    return lang < 0 ? 0 : lang;
}

/* --keys: what the keyboard and pad press, for checking the navigation in a --shot */
static ImGuiKey key_of(const std::string &k) {
    static const struct { const char *name; ImGuiKey key; } KEYS[] = {
        {"down", ImGuiKey_DownArrow}, {"up", ImGuiKey_UpArrow}, {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow},
        {"enter", ImGuiKey_Enter}, {"space", ImGuiKey_Space}, {"tab", ImGuiKey_Tab}, {"esc", ImGuiKey_Escape},
        {"pad-down", ImGuiKey_GamepadDpadDown}, {"pad-up", ImGuiKey_GamepadDpadUp}, {"pad-left", ImGuiKey_GamepadDpadLeft},
        {"pad-right", ImGuiKey_GamepadDpadRight}, {"pad-a", ImGuiKey_GamepadFaceDown}, {"pad-b", ImGuiKey_GamepadFaceRight},
        {"pad-rb", ImGuiKey_GamepadR1}, {"pgdn", ImGuiKey_PageDown},
    };
    for (const auto &e : KEYS) {
        if (k == e.name) return e.key;
    }
    SDL_Log("unknown key %s", k.c_str());
    return ImGuiKey_None;
}

int main(int argc, char **argv) {
    const char *shot = nullptr;
    float dpi = 0;
    int w = 960, h = 600, lang = -1;
    std::vector<ImGuiKey> keys;

    for (int i = 1; i + 1 < argc; i += 2) {
        std::string a = argv[i], v = argv[i + 1];
        if (a == "--lang") lang = SDL_clamp(SDL_atoi(v.c_str()), 0, LANGS - 1);
        else if (a == "--state") g_state = v == "nogame" ? ST_NOGAME : v == "error" ? ST_ERROR : ST_READY;
        else if (a == "--page") g_page = v == "settings" ? PAGE_SETTINGS : PAGE_PLAY;
        else if (a == "--dpi") dpi = (float)SDL_atof(v.c_str());
        else if (a == "--size") SDL_sscanf(v.c_str(), "%dx%d", &w, &h);
        else if (a == "--shot") shot = argv[i + 1];
        else if (a == "--keys") {
            size_t from = 0;
            while (from <= v.size()) {
                size_t to = SDL_min(v.find(',', from), v.size());
                keys.push_back(key_of(v.substr(from, to - from)));
                from = to + 1;
            }
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2003", SDL_GetError(), NULL);
        return 1;
    }
    g_lang = lang >= 0 ? lang : system_lang();
    float scale = dpi > 0 ? dpi : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    SDL_Window *win;
    SDL_Renderer *r;
    if (!SDL_CreateWindowAndRenderer("Digimon World 2003", (int)(w * scale), (int)(h * scale), SDL_WINDOW_RESIZABLE | (shot ? SDL_WINDOW_HIDDEN : 0), &win, &r)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2003", SDL_GetError(), NULL);
        return 1;
    }
    SDL_SetWindowMinimumSize(win, (int)(760 * scale), (int)(480 * scale));
    SDL_SetRenderVSync(r, 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForSDLRenderer(win, r);
    ImGui_ImplSDLRenderer3_Init(r);
    set_style();
    apply_scale(scale);

    bool simulated_pad = false;
    for (ImGuiKey k : keys) simulated_pad |= k >= ImGuiKey_GamepadStart && k <= ImGuiKey_GamepadR3;
    int shown = 0, total = 8 + (int)keys.size() * 4;
    float rescale = 0;
    bool running = true;
    while (running) {
        SDL_Event e;
        /* asleep until something happens, but for a slow tick: a page that doesn't change costs nothing */
        bool have = shot ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, 100);
        while (have) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED && dpi <= 0) rescale = SDL_GetWindowDisplayScale(win);
            have = SDL_PollEvent(&e);
        }
        if (rescale > 0 && rescale != g_s) {
            apply_scale(rescale);
        }
        rescale = 0;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (simulated_pad) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        if (shot) { /* a key down for two frames, up for two */
            int at = shown - 4, n = at / 4;
            if (at >= 0 && n < (int)keys.size() && at % 4 == 0) io.AddKeyEvent(keys[n], true);
            if (at >= 0 && n < (int)keys.size() && at % 4 == 2) io.AddKeyEvent(keys[n], false);
        }
        ImGui::NewFrame();
        /* with a pad at hand, the focus shows from the start */
        static int frames = 0;
        if (++frames == 3 && (simulated_pad || SDL_HasGamepad())) ImGui::SetNavCursorVisible(true);
        ImGui::PushFont(g_body);
        frame();
        ImGui::PopFont();
        ImGui::Render();
        SDL_SetRenderDrawColor(r, 8, 13, 26, 255);
        SDL_RenderClear(r);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), r);
        if (shot && ++shown >= total) {
            SDL_Surface *px = SDL_RenderReadPixels(r, NULL);
            if (px == NULL || !SDL_SavePNG(px, shot)) {
                fprintf(stderr, "shot: %s\n", SDL_GetError());
                return 1;
            }
            SDL_DestroySurface(px);
            running = false;
        }
        SDL_RenderPresent(r);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
