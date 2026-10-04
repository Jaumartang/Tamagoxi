#include "games.h"

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

#include "audio.h"
#include "display.h"
#include "pet.h"
#include "tg_config.h"
#include "ui.h"

namespace {

TFT_eSPI& tft()
{
    return Display::driver();
}

/* Colors vius (per als jocs, que han de cridar l'atencio d'una nena). */
constexpr uint16_t kDark   = 0x4208;
constexpr uint16_t kWhite  = TFT_WHITE;
constexpr uint16_t kGood   = 0x07E0;   /* verd */
constexpr uint16_t kBad    = 0xF800;   /* vermell */
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kOrange = 0xFD20;
constexpr uint16_t kCyan   = 0x07FF;
constexpr uint16_t kBlue   = 0x2C7F;
constexpr uint16_t kPink   = 0xFDB8;
constexpr uint16_t kPurple = 0x9817;
constexpr uint16_t kBrown  = 0x9A60;

/* --- Dibuixos fets amb primitives (per no dependre de cap imatge) ---------- */

void drawPic(uint8_t id, int cx, int cy, int s)
{
    TFT_eSPI& t = tft();
    const int r = s / 2;
    switch (id) {
        case 1:      /* SOL */
            t.fillCircle(cx, cy, r, kYellow);
            for (int a = 0; a < 8; ++a) {
                const float ang = a * 0.785f;
                t.drawLine(cx + static_cast<int>(cosf(ang) * (r + 2)),
                           cy + static_cast<int>(sinf(ang) * (r + 2)),
                           cx + static_cast<int>(cosf(ang) * (r + 10)),
                           cy + static_cast<int>(sinf(ang) * (r + 10)), kOrange);
            }
            break;
        case 2:      /* CASA */
            t.fillRect(cx - r, cy, s, r, 0xFD20);
            t.fillTriangle(cx - r - 3, cy, cx + r + 3, cy, cx, cy - r, kBad);
            t.fillRect(cx - r / 3, cy + r / 2, r / 2, r / 2, kBrown);
            break;
        case 3:      /* PEIX */
            t.fillCircle(cx - r / 3, cy, r / 2 + 3, kOrange);
            t.fillTriangle(cx + r / 3, cy - r / 2, cx + r / 3, cy + r / 2, cx + r, cy, kOrange);
            t.fillCircle(cx - r / 2, cy - 3, 2, kDark);
            break;
        case 4:      /* FLOR */
            for (int a = 0; a < 5; ++a) {
                const float ang = a * 1.2566f;
                t.fillCircle(cx + static_cast<int>(cosf(ang) * r * 0.6f),
                             cy + static_cast<int>(sinf(ang) * r * 0.6f), r / 3, kPink);
            }
            t.fillCircle(cx, cy, r / 3, kYellow);
            break;
        case 5:      /* LLUNA */
            t.fillCircle(cx, cy, r, kYellow);
            t.fillCircle(cx + r / 2, cy - r / 4, r, 0x18E3);   /* mossegada */
            break;
        case 6:      /* ESTRELLA */
            t.fillTriangle(cx - r, cy + r / 2, cx + r, cy + r / 2, cx, cy - r, kYellow);
            t.fillTriangle(cx - r, cy - r / 3, cx + r, cy - r / 3, cx, cy + r, kYellow);
            break;
        case 7:      /* POMA */
            t.fillCircle(cx, cy + 2, r, kBad);
            t.drawLine(cx, cy - r + 2, cx, cy - r - 6, kBrown);
            t.fillTriangle(cx + 1, cy - r - 4, cx + 10, cy - r - 9, cx + 12, cy - r + 1, kGood);
            break;
        case 8:      /* COR */
            t.fillCircle(cx - r / 2, cy - r / 3, r / 2, kBad);
            t.fillCircle(cx + r / 2, cy - r / 3, r / 2, kBad);
            t.fillTriangle(cx - r, cy - r / 4, cx + r, cy - r / 4, cx, cy + r, kBad);
            break;
        case 9:      /* NUVOL */
            t.fillCircle(cx - r / 2, cy, r / 2, kWhite);
            t.fillCircle(cx + r / 2, cy, r / 2, kWhite);
            t.fillCircle(cx, cy - r / 3, r / 2 + 2, kWhite);
            t.fillRect(cx - r / 2, cy, r, r / 2, kWhite);
            break;
        case 10:     /* GAT */
            t.fillCircle(cx, cy + 2, r - 2, 0x8410);
            t.fillTriangle(cx - r, cy - r / 2, cx - r / 3, cy - r / 2, cx - r, cy - r - 4, 0x8410);
            t.fillTriangle(cx + r, cy - r / 2, cx + r / 3, cy - r / 2, cx + r, cy - r - 4, 0x8410);
            t.fillCircle(cx - r / 3, cy - 2, 2, kDark);
            t.fillCircle(cx + r / 3, cy - 2, 2, kDark);
            break;
        case 11:     /* ARBRE */
            t.fillRect(cx - 5, cy, 10, r, kBrown);
            t.fillCircle(cx, cy - r / 2, r - 3, kGood);
            break;
        case 12:     /* PILOTA */
            t.fillCircle(cx, cy, r, kCyan);
            t.drawCircle(cx, cy, r / 2, kBlue);
            t.drawLine(cx - r, cy, cx + r, cy, kBlue);
            break;
        case 13:     /* BARCA */
            t.fillTriangle(cx, cy - r, cx, cy, cx + r, cy, kWhite);
            t.fillRect(cx - 2, cy - r, 3, r, kBrown);
            t.fillTriangle(cx - r, cy + 2, cx + r, cy + 2, cx, cy + r / 2, kBad);
            break;
        default:
            break;
    }
}

/* --- Paraules dels jocs de lletres --------------------------------------- */

struct Word {
    const char* text;
    uint8_t     pic;
};

const Word kWords[] = {
    {"SOL", 1}, {"CASA", 2}, {"PEIX", 3}, {"FLOR", 4}, {"LLUNA", 5}, {"ESTRELLA", 6},
    {"POMA", 7}, {"COR", 8}, {"NUVOL", 9}, {"GAT", 10}, {"ARBRE", 11}, {"PILOTA", 12},
};
constexpr uint8_t kWordCount = sizeof(kWords) / sizeof(kWords[0]);


/* --- Estat, recompenses i motor de preguntes ------------------------------- */

constexpr const char* kNamespace = "tg_play";
constexpr uint8_t kNumGames = 5;

/* Una pregunta del motor comu: tots els jocs en generen una d'aquesta forma. */
struct Question {
    char    title[22];      /* "3 + 4 = ?"  o  "COMENCA PER..." */
    char    word[12];       /* la paraula (jocs de lletres) o buit */
    uint8_t pic;            /* dibuix (0 = cap) */
    uint8_t kind;           /* 1 = lletra gran, 0 = text normal */
    uint8_t nOptions;
    char    options[3][12];
    uint8_t correct;
};

uint16_t gStars        = 0;      /* estrelles guanyades (desades a la NVS) */
uint16_t gStarsSession = 0;
uint8_t  gRight[kNumGames] = {0};
uint8_t  gCurrent  = 0xFF;       /* joc obert (0xFF = cap) */
uint8_t  gStreak   = 0;
uint8_t  gSinceCelebration = 0;
Question gQ;
bool     gAnswered = false;
uint8_t  gChosen   = 0xFF;
uint32_t gFeedbackMs = 0;
uint32_t gNextMs     = 0;
bool     gCelebrating = false;

void saveRewards()
{
    Preferences p;
    if (!p.begin(kNamespace, false)) {
        return;
    }
    p.putUShort("stars", gStars);
    p.putBytes("right", gRight, sizeof(gRight));
    p.end();
}

void loadRewards()
{
    Preferences p;
    if (!p.begin(kNamespace, true)) {
        return;
    }
    gStars = p.getUShort("stars", 0);
    const size_t n = p.getBytesLength("right");
    if (n == sizeof(gRight)) {
        p.getBytes("right", gRight, sizeof(gRight));
    }
    p.end();
}

/* --- Generadors de preguntes (un per joc) --------------------------------- */

/* Posa les tres opcions i barreja-les, perque la bona no surti sempre al mateix lloc. */
void setOptions(const char* a, const char* b, const char* c)
{
    char tmp[3][12];
    strlcpy(tmp[0], a, sizeof(tmp[0]));
    strlcpy(tmp[1], b, sizeof(tmp[1]));
    strlcpy(tmp[2], c, sizeof(tmp[2]));
    const uint8_t rot = static_cast<uint8_t>(random(3));
    for (uint8_t i = 0; i < 3; ++i) {
        strlcpy(gQ.options[i], tmp[(i + rot) % 3], sizeof(gQ.options[i]));
    }
    gQ.correct = static_cast<uint8_t>((3 - rot) % 3);   /* la bona (index 0 de tmp) */
    gQ.nOptions = 3;
    gQ.kind     = 0;
    gQ.pic      = 0;
    gQ.word[0]  = '\0';
}

/* Dificultat segons les estrelles: el joc es fa mes dificil a mesura que juga. */
uint8_t difficulty()
{
    const uint8_t lvl = static_cast<uint8_t>(gStars / 10);
    return (lvl > 4) ? 4 : lvl;
}

/* 1) SUMES */
void genSum()
{
    const int top = 5 + difficulty() * 3;          /* 5 .. 17 */
    const int a = 1 + static_cast<int>(random(static_cast<uint32_t>(top - 1)));
    const int b = 1 + static_cast<int>(random(static_cast<uint32_t>(top - a > 1 ? top - a : 2)));
    const int s = a + b;
    snprintf(gQ.title, sizeof(gQ.title), "%d + %d = ?", a, b);
    char o[3][12];
    snprintf(o[0], sizeof(o[0]), "%d", s);
    snprintf(o[1], sizeof(o[1]), "%d", s + 1 + static_cast<int>(random(2)));
    snprintf(o[2], sizeof(o[2]), "%d", (s > 1) ? (s - 1) : (s + 3));
    setOptions(o[0], o[1], o[2]);
}

/* 2) RESTES */
void genSub()
{
    const int top = 4 + difficulty() * 3;          /* 4 .. 16 */
    const int a = 2 + static_cast<int>(random(static_cast<uint32_t>(top)));
    const int b = 1 + static_cast<int>(random(static_cast<uint32_t>(a - 1)));
    const int r = a - b;
    snprintf(gQ.title, sizeof(gQ.title), "%d - %d = ?", a, b);
    char o[3][12];
    snprintf(o[0], sizeof(o[0]), "%d", r);
    snprintf(o[1], sizeof(o[1]), "%d", r + 1);
    snprintf(o[2], sizeof(o[2]), "%d", r + 2);
    setOptions(o[0], o[1], o[2]);
}

/* 3) LLETRES: amb quina lletra comenca aquest dibuix? */
void genLetter()
{
    const uint8_t w = static_cast<uint8_t>(random(kWordCount));
    gQ.pic = kWords[w].pic;
    strlcpy(gQ.title, "COMENCA PER...?", sizeof(gQ.title));
    strlcpy(gQ.word, kWords[w].text, sizeof(gQ.word));
    gQ.kind = 1;                                    /* lletres grans */

    const char good = kWords[w].text[0];
    char w1 = static_cast<char>('A' + random(26));
    while (w1 == good) {
        w1 = static_cast<char>('A' + random(26));
    }
    char w2 = static_cast<char>('A' + random(26));
    while (w2 == good || w2 == w1) {
        w2 = static_cast<char>('A' + random(26));
    }
    char o0[2] = {good, 0};
    char o1[2] = {w1, 0};
    char o2[2] = {w2, 0};
    setOptions(o0, o1, o2);
    gQ.kind = 1;                                    /* setOptions ho posa a 0 */
}

/* 4) PARAULES: quin dibuix es? (llegir) */
void genWord()
{
    const uint8_t w = static_cast<uint8_t>(random(kWordCount));
    uint8_t w2 = static_cast<uint8_t>(random(kWordCount));
    while (w2 == w) {
        w2 = static_cast<uint8_t>(random(kWordCount));
    }
    uint8_t w3 = static_cast<uint8_t>(random(kWordCount));
    while (w3 == w || w3 == w2) {
        w3 = static_cast<uint8_t>(random(kWordCount));
    }
    gQ.pic = kWords[w].pic;
    strlcpy(gQ.title, "QUINA PARAULA ES?", sizeof(gQ.title));
    setOptions(kWords[w].text, kWords[w2].text, kWords[w3].text);
    gQ.pic = kWords[w].pic;                         /* setOptions l'esborra */
}

/* 5) ESCRIURE: quina lletra falta? */
void genMissing()
{
    uint8_t w = static_cast<uint8_t>(random(kWordCount));
    while (strlen(kWords[w].text) < 3 || strlen(kWords[w].text) > 6) {
        w = static_cast<uint8_t>(random(kWordCount));
    }
    const size_t len = strlen(kWords[w].text);
    const size_t pos = 1 + static_cast<size_t>(random(static_cast<uint32_t>(len - 1)));
    char shown[12];
    strlcpy(shown, kWords[w].text, sizeof(shown));
    const char miss = shown[pos];
    shown[pos] = '_';

    gQ.pic = kWords[w].pic;
    strlcpy(gQ.title, "QUINA LLETRA FALTA?", sizeof(gQ.title));
    strlcpy(gQ.word, shown, sizeof(gQ.word));

    char w1 = static_cast<char>('A' + random(26));
    while (w1 == miss) {
        w1 = static_cast<char>('A' + random(26));
    }
    char w2 = static_cast<char>('A' + random(26));
    while (w2 == miss || w2 == w1) {
        w2 = static_cast<char>('A' + random(26));
    }
    char o0[2] = {miss, 0};
    char o1[2] = {w1, 0};
    char o2[2] = {w2, 0};
    setOptions(o0, o1, o2);
    gQ.kind = 1;
    gQ.pic  = kWords[w].pic;
}

/* --- Taula de jocs: ESCALABLE (afegir un joc = una linia) ----------------- */

const Games::Definition kGames[] = {
    {"Sumes",    "+",   genSum},
    {"Restes",   "-",   genSub},
    {"Lletres",  "A",   genLetter},
    {"Paraules", "abc", genWord},
    {"Escriure", "_",   genMissing},
};
constexpr uint8_t kGameCount = sizeof(kGames) / sizeof(kGames[0]);




/* --- Dibuix comu de la pantalla del joc ----------------------------------- */

void drawHeader(const char* title)
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, 52, kPurple);
    t.setTextDatum(ML_DATUM);
    t.setTextFont(4);
    t.setTextColor(kWhite, kPurple);
    t.drawString(title, 62, 27);

    /* Boto enrere (gran: es per a un dit petit). */
    t.fillRoundRect(6, 7, 48, 38, 10, kBad);
    t.drawRoundRect(6, 7, 48, 38, 10, kWhite);
    t.setTextDatum(MC_DATUM);
    t.setTextFont(4);
    t.setTextColor(kWhite, kBad);
    t.drawString("<", 30, 26);

    /* Estrelles guanyades. */
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(gStars));
    t.setTextDatum(MR_DATUM);
    t.setTextFont(4);
    t.setTextColor(kYellow, kPurple);
    t.drawString(buf, SCREEN_W - 12, 27);
    t.fillTriangle(SCREEN_W - 76, 34, SCREEN_W - 54, 34, SCREEN_W - 65, 14, kYellow);
    t.fillTriangle(SCREEN_W - 78, 26, SCREEN_W - 52, 26, SCREEN_W - 65, 44, kYellow);
}

void drawOptionCard(uint8_t i, uint16_t col)
{
    TFT_eSPI& t = tft();
    const int y = 246 + i * 76;
    t.fillRoundRect(24, y, SCREEN_W - 48, 66, 16, col);
    t.drawRoundRect(24, y, SCREEN_W - 48, 66, 16, kWhite);
    t.setTextDatum(MC_DATUM);
    t.setTextFont(6);
    t.setTextColor(kDark, col);
    t.drawString(gQ.options[i], SCREEN_W / 2, y + 33);
}

void drawQuestion()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 52, SCREEN_W, SCREEN_H - 52, kBlue);

    if (gQ.pic != 0) {
        drawPic(gQ.pic, SCREEN_W / 2, 142, 86);
    }

    t.setTextDatum(MC_DATUM);
    t.setTextFont(4);
    t.setTextColor(kWhite, kBlue);
    t.drawString(gQ.title, SCREEN_W / 2, (gQ.pic != 0) ? 208 : 140);

    if (gQ.word[0] != '\0') {
        t.setTextFont(6);
        t.setTextColor(kYellow, kBlue);
        t.drawString(gQ.word, SCREEN_W / 2, (gQ.pic != 0) ? 168 : 208);
    }

    for (uint8_t i = 0; i < gQ.nOptions; ++i) {
        drawOptionCard(i, (i == 0) ? kPink : ((i == 1) ? kCyan : kOrange));
    }
}

/* Pregunta nova del joc en curs. */
void nextQuestion()
{
    gAnswered = false;
    gChosen   = 0xFF;
    if (gCurrent >= kGameCount) {
        return;
    }
    kGames[gCurrent].generate();
    drawQuestion();
}

/* Pantalla de celebracio cada 10 estrelles. */
void drawCelebration()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 52, SCREEN_W, SCREEN_H - 52, kPurple);
    drawPic(6, SCREEN_W / 2, 148, 110);            /* estrella gran */
    t.setTextDatum(MC_DATUM);
    t.setTextFont(6);
    t.setTextColor(kYellow, kPurple);
    t.drawString("MOLT BE!", SCREEN_W / 2, 250);
    t.setTextFont(2);
    t.setTextColor(kWhite, kPurple);
    char buf[40];
    snprintf(buf, sizeof(buf), "Ja tens %u estrelles!", static_cast<unsigned>(gStars));
    t.drawString(buf, SCREEN_W / 2, 296);
    t.drawString("El drac esta molt content!", SCREEN_W / 2, 322);
    t.setTextColor(kYellow, kPurple);
    t.drawString("Toca la pantalla per continuar", SCREEN_W / 2, 420);
}

void nextQuestion();

/* Ha tocat una opcio. */
void answer(uint8_t i)
{
    if (gAnswered) {
        return;
    }
    gAnswered   = true;
    gChosen     = i;
    gFeedbackMs = millis();

    const bool ok = (i == gQ.correct);
    if (ok) {
        ++gStars;
        ++gStarsSession;
        ++gStreak;
        ++gSinceCelebration;
        if (gRight[gCurrent] < 250) {
            ++gRight[gCurrent];
        }
        saveRewards();
        /* El drac tambe celebra... pero no a cada encert: jugar gasta energia i
         * el deixariem esgotat. Cada 5 estrelles. */
        if ((gStars % 5) == 0) {
            Pet::play();
        }
        Audio::beep(880, 90, 70);
        delay(110);
        Audio::beep(1320, 130, 70);
    } else {
        gStreak = 0;
        Audio::beep(300, 260, 70);
    }

    /* Repinta les targetes amb el resultat. */
    for (uint8_t k = 0; k < gQ.nOptions; ++k) {
        uint16_t col = (k == 0) ? kPink : ((k == 1) ? kCyan : kOrange);
        if (k == gQ.correct) {
            col = kGood;               /* la bona sempre en verd */
        } else if (k == gChosen) {
            col = kBad;                /* la que ha tocat, si es equivocada */
        }
        drawOptionCard(k, col);
    }

    gNextMs = millis() + (ok ? 900 : 1800);
}

}  // namespace

namespace Games {

void begin()
{
    randomSeed(micros());
    loadRewards();
    Serial.printf("[JOC] %u jocs, %u estrelles guanyades\n", static_cast<unsigned>(kGameCount),
                  static_cast<unsigned>(gStars));
}

uint8_t count()
{
    return kGameCount;
}

const Definition& game(uint8_t index)
{
    return kGames[(index < kGameCount) ? index : 0];
}

void open(uint8_t index)
{
    if (index >= kGameCount) {
        return;
    }
    gCurrent  = index;
    gAnswered = false;
    gStreak   = 0;
    drawHeader(kGames[index].name);
    nextQuestion();
    Serial.printf("[JOC] comencant \"%s\" (nivell %u, %u estrelles)\n", kGames[index].name,
                  static_cast<unsigned>(difficulty() + 1), static_cast<unsigned>(gStars));
}

void close()
{
    if (gCurrent != 0xFF) {
        Serial.printf("[JOC] fi del joc (%u encerts, %u estrelles)\n",
                      static_cast<unsigned>(gRight[gCurrent]),
                      static_cast<unsigned>(gStars));
    }
    gCurrent = 0xFF;
}

bool isOpen()
{
    return gCurrent != 0xFF;
}

uint8_t current()
{
    return gCurrent;
}

void loop(uint32_t nowMs)
{
    if (gCurrent == 0xFF || gCelebrating) {
        return;
    }
    if (gAnswered && static_cast<int32_t>(nowMs - gNextMs) >= 0) {
        if (gSinceCelebration >= 10) {
            gSinceCelebration = 0;
            gCelebrating      = true;
            drawCelebration();
            Pet::feed();                 /* premi gros: el drac menja */
            return;
        }
        nextQuestion();
    }
}

bool handleTap(int16_t x, int16_t y)
{
    if (gCurrent == 0xFF) {
        return false;
    }
    /* El boto "enrere" de dalt sempre funciona. */
    if (y < 52 && x < 62) {
        Serial.println(F("[JOC] enrere -> menu de jocs"));
        Audio::beep(520, 90, 60);
        return false;                    /* el panell torna al menu de jocs */
    }
    if (gCelebrating) {
        gCelebrating = false;
        nextQuestion();
        return true;
    }
    if (gAnswered) {
        return true;                     /* ja ha respost: esperem */
    }
    for (uint8_t i = 0; i < gQ.nOptions; ++i) {
        const int y0 = 246 + i * 76;
        if (x >= 24 && x < SCREEN_W - 24 && y >= y0 && y < y0 + 66) {
            answer(i);
            return true;
        }
    }
    return true;
}

uint16_t stars()
{
    return gStars;
}

uint16_t starsThisSession()
{
    return gStarsSession;
}

uint8_t rightOf(uint8_t index)
{
    return (index < kGameCount) ? gRight[index] : 0;
}

uint16_t level()
{
    return static_cast<uint16_t>(gStars / 10);
}

void resetProgress()
{
    gStars        = 0;
    gStarsSession = 0;
    memset(gRight, 0, sizeof(gRight));
    saveRewards();
    Serial.println(F("[JOC] progres esborrat (com si comences de zero)"));
}

void printStatus()
{
    Serial.printf("[JOC] %u jocs | %u estrelles (nivell %u) | aquesta estona: %u\n",
                  static_cast<unsigned>(kGameCount), static_cast<unsigned>(gStars),
                  static_cast<unsigned>(level() + 1),
                  static_cast<unsigned>(gStarsSession));
    for (uint8_t i = 0; i < kGameCount; ++i) {
        Serial.printf("  %u. %-9s %u encerts\n", static_cast<unsigned>(i), kGames[i].name,
                      static_cast<unsigned>(gRight[i]));
    }
}

}  // namespace Games
