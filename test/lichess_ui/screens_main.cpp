// SPDX-License-Identifier: GPL-3.0-or-later
// What the Lichess screens add, tested without a screen session: usernames, the recent opponents
// and their blob (every corruption refused), the keyboard's keys, wrapped and fitted text, the
// waits as the screens say them, which invitations the board can play, the invitations list,
// and the QR codes drawn into a real 800x480 canvas, checked module by module.
// argv[1] is a directory: the codes are also written there as PNG files, with qr_expected.txt
// listing what each must say, for the Makefile to read them back with Core Image.
#include <Adafruit_GFX.h>

#include <cstdio>
#include <cstring>

#include "../lichess/fake_transport.h"
#include "arrocco/platform.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/lichess_friends.h"
#include "arrocco/ui/lichess_page.h"
#include "arrocco/ui/lichess_state.h"
#include "arrocco/ui/name_keyboard.h"
#include "arrocco/ui/qr_code.h"
#include "arrocco/ui/saved_game.h"

namespace {

using arrocco::kScreenH;
using arrocco::kScreenW;
using namespace arrocco::ui;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL %s\n", what);
  }
}

bool ink(GFXcanvas1& c, int x, int y) { return !c.getPixel(static_cast<int16_t>(x), static_cast<int16_t>(y)); }

int inkIn(GFXcanvas1& c, int x0, int y0, int w, int h) {
  int n = 0;
  for (int y = y0; y < y0 + h; ++y)
    for (int x = x0; x < x0 + w; ++x)
      if (x >= 0 && y >= 0 && x < kScreenW && y < kScreenH && ink(c, x, y)) ++n;
  return n;
}

// ---- a PNG of the canvas, deflate "stored" blocks: no zlib needed ------------------------------
void put32(FILE* f, uint32_t v) {
  const uint8_t b[4] = {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
                        static_cast<uint8_t>(v)};
  std::fwrite(b, 1, 4, f);
}

void chunk(FILE* f, const char* type, const uint8_t* data, size_t size) {
  static uint8_t buffer[60000];
  std::memcpy(buffer, type, 4);
  std::memcpy(buffer + 4, data, size);
  put32(f, static_cast<uint32_t>(size));
  std::fwrite(buffer, 1, size + 4, f);
  put32(f, crc32(buffer, size + 4));
}

bool writePng(GFXcanvas1& c, const char* path) {
  FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::fwrite(kSignature, 1, 8, f);
  const uint8_t header[13] = {0, 0, 0x03, 0x20, 0, 0, 0x01, 0xE0, 1, 0, 0, 0, 0};   // 800 x 480, 1 bit, grey
  chunk(f, "IHDR", header, sizeof header);
  // The raw scanlines (filter byte 0, then the row: bit 1 = white, as GFXcanvas1 keeps them).
  static uint8_t raw[(1 + kScreenW / 8) * kScreenH];
  const uint8_t* bits = c.getBuffer();
  for (int y = 0; y < kScreenH; ++y) {
    raw[y * (1 + kScreenW / 8)] = 0;
    std::memcpy(raw + y * (1 + kScreenW / 8) + 1, bits + y * (kScreenW / 8), kScreenW / 8);
  }
  const size_t rawSize = sizeof raw;   // 48,480: one stored block holds up to 65,535
  static uint8_t zlib[sizeof raw + 16];
  size_t n = 0;
  zlib[n++] = 0x78;
  zlib[n++] = 0x01;
  zlib[n++] = 0x01;   // final block, stored
  zlib[n++] = static_cast<uint8_t>(rawSize);
  zlib[n++] = static_cast<uint8_t>(rawSize >> 8);
  zlib[n++] = static_cast<uint8_t>(~rawSize);
  zlib[n++] = static_cast<uint8_t>(~rawSize >> 8);
  std::memcpy(zlib + n, raw, rawSize);
  n += rawSize;
  uint32_t a = 1, b = 0;   // Adler-32
  for (size_t i = 0; i < rawSize; ++i) {
    a = (a + raw[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  const uint32_t adler = (b << 16) | a;
  zlib[n++] = static_cast<uint8_t>(adler >> 24);
  zlib[n++] = static_cast<uint8_t>(adler >> 16);
  zlib[n++] = static_cast<uint8_t>(adler >> 8);
  zlib[n++] = static_cast<uint8_t>(adler);
  chunk(f, "IDAT", zlib, n);
  chunk(f, "IEND", nullptr, 0);
  std::fclose(f);
  return true;
}

// ---- usernames and the recent opponents ------------------------------------------------------------
void testUsernames() {
  check(validUsername("ab"), "two characters is a username");
  check(validUsername("amico_99") && validUsername("A-b_C"), "letters, digits, '-' and '_'");
  check(validUsername("abcdefghijklmnopqrstuvwxyz0123"), "thirty characters");
  check(!validUsername("abcdefghijklmnopqrstuvwxyz01234"), "thirty-one are too many");
  check(!validUsername("") && !validUsername("a") && !validUsername(nullptr), "empty, one letter, nothing");
  check(!validUsername("bad name") && !validUsername("../api") && !validUsername("caf\xc3\xa9"),
        "a space, a path, a letter Lichess does not allow");
  check(sameUsername("Amico", "aMICO") && !sameUsername("amico", "amica") && !sameUsername("amico", "amico2"),
        "names compare without case, and whole");
}

void testRecentFriends() {
  RecentFriends f;
  check(f.count() == 0 && std::strcmp(f.at(0), "") == 0, "an empty list");
  check(f.add("amico") && f.add("rita") && f.count() == 2, "two names");
  check(std::strcmp(f.at(0), "rita") == 0 && std::strcmp(f.at(1), "amico") == 0, "the newest first");
  check(f.add("AMICO") && f.count() == 2 && std::strcmp(f.at(0), "AMICO") == 0 && std::strcmp(f.at(1), "rita") == 0,
        "an old name moves up, spelt the new way, never twice");
  check(!f.add("no way") && f.count() == 2, "an invalid name changes nothing");
  f.add("c3");
  f.add("d4");
  f.add("e5");
  check(f.count() == kMaxFriends, "never more than four");
  check(std::strcmp(f.at(0), "e5") == 0 && std::strcmp(f.at(3), "AMICO") == 0, "the oldest (rita) fell off");
  check(std::strcmp(f.at(4), "") == 0 && std::strcmp(f.at(-1), "") == 0, "out of range is empty");

  uint8_t blob[kFriendsMaxBytes];
  const size_t size = f.encode(blob, sizeof blob);
  check(size > 10 && size <= kFriendsMaxBytes, "the list encodes");
  RecentFriends g;
  check(g.decode(blob, size) && g.count() == 4, "and decodes");
  bool same = true;
  for (int i = 0; i < 4; ++i) same = same && std::strcmp(f.at(i), g.at(i)) == 0;
  check(same, "to the same names, in the same order");
  check(f.encode(blob, 8) == 0, "a buffer too small is refused");

  // Every corruption is refused, and leaves the list empty.
  uint8_t bad[kFriendsMaxBytes];
  auto refused = [&](const char* what) {
    RecentFriends h;
    h.add("left");
    char text[96];
    std::snprintf(text, sizeof text, "refused: %s", what);
    check(!h.decode(bad, size) && h.count() == 0, text);
  };
  std::memcpy(bad, blob, size);
  bad[0] = 'X';
  refused("another magic");
  std::memcpy(bad, blob, size);
  bad[4] = 2;
  refused("another version");
  std::memcpy(bad, blob, size);
  bad[8] ^= 0x20;
  refused("a flipped bit");
  // Changes with a good CRC: the layout and the names themselves are checked too.
  auto reseal = [&]() {
    const uint32_t crc = crc32(bad, size - 4);
    for (int i = 0; i < 4; ++i) bad[size - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
  };
  std::memcpy(bad, blob, size);
  bad[5] = 5;
  reseal();
  refused("five names");
  std::memcpy(bad, blob, size);
  bad[7] = ' ';
  reseal();
  refused("a name with a space");
  std::memcpy(bad, blob, size);
  bad[6] = 40;
  reseal();
  refused("a name longer than the blob");
  RecentFriends h;
  check(!h.decode(blob, size - 1) && !h.decode(blob, 3) && !h.decode(nullptr, size), "a cut blob, a crumb, nothing");
}

// The flash behind loadFriends()/saveFriends(): one blob, kept in memory.
class StorePlatform final : public arrocco::Platform {
 public:
  StorePlatform() : canvas_(kScreenW, kScreenH) {}
  Adafruit_GFX& gfx() override { return canvas_; }
  void present(arrocco::Refresh) override {}
  void panelOff() override {}
  uint32_t millis() override { return 0; }
  void beep(uint16_t, uint16_t) override {}
  int batteryPercent() override { return -1; }
  bool usbPowered() override { return false; }
  size_t loadBlob(const char* key, uint8_t* out, size_t capacity) override {
    if (std::strcmp(key, kFriendsKey) != 0 || size_ == 0 || size_ > capacity) return 0;
    std::memcpy(out, data_, size_);
    return size_;
  }
  bool storeBlob(const char* key, const uint8_t* data, size_t size) override {
    if (std::strcmp(key, kFriendsKey) != 0 || size > sizeof data_) return false;
    std::memcpy(data_, data, size);
    size_ = size;
    ++writes;
    return true;
  }
  int writes = 0;

 private:
  GFXcanvas1 canvas_;
  uint8_t data_[256] = {};
  size_t size_ = 0;
};

class NoAccount final : public arrocco::lichess::Account {
 public:
  arrocco::lichess::WifiStatus wifiStatus() override { return arrocco::lichess::WifiStatus::Online; }
  const char* wifiDetail() override { return ""; }
  const char* portalName() override { return "Arrocco-TEST"; }
  const char* portalAddress() override { return "http://4.3.2.1"; }
  void openPortal() override {}
  void closePortal() override {}
  void wantNetwork() override {}
  bool linked() override { return true; }
  bool beginLogin(char*, int) override { return false; }
  arrocco::lichess::LoginStatus loginStatus() override { return arrocco::lichess::LoginStatus::Idle; }
  const char* loginError() override { return ""; }
  void cancelLogin() override {}
  void unlink() override {}
};

void testFriendsInFlash() {
  StorePlatform platform;
  arrocco_test::FakeTransport transport;
  NoAccount account;
  static LichessState first(transport, account);
  first.loadFriends(platform);
  check(first.friends.count() == 0, "a board fresh from the factory knows nobody");
  first.rememberOpponent("amico");
  first.saveFriends(platform);
  check(platform.writes == 1, "a new opponent is one write");
  first.rememberOpponent("amico");
  first.saveFriends(platform);
  check(platform.writes == 1, "the same one again is none");
  first.rememberOpponent("Stockfish level 3");
  first.saveFriends(platform);
  check(platform.writes == 1, "a name that is not a username is not kept");
  first.rememberOpponent("rita");
  first.saveFriends(platform);
  static LichessState second(transport, account);
  second.loadFriends(platform);
  check(second.friends.count() == 2 && std::strcmp(second.friends.at(0), "rita") == 0,
        "the next start reads them back, newest first");
}

// ---- the challenges the board can play, and the ones sent to us -----------------------------------------
void testInvitations() {
  arrocco::lichess::ChallengeInfo c;
  std::strcpy(c.variant, "standard");
  c.clockLimit = 300;
  c.clockIncrement = 3;
  check(LichessState::playable(c), "5 + 3 standard");
  c.clockLimit = 180;
  c.clockIncrement = 0;
  check(LichessState::playable(c), "3 + 0 is the fastest the Board API takes");
  c.clockLimit = 120;
  c.clockIncrement = 1;
  check(!LichessState::playable(c), "2 + 1 is bullet: refused");
  c.clockLimit = -1;
  check(LichessState::playable(c), "correspondence: no clock to refuse");
  std::strcpy(c.variant, "chess960");
  check(!LichessState::playable(c), "a variant the rules do not know");
  std::strcpy(c.variant, "fromPosition");
  check(LichessState::playable(c), "standard chess from a position");

  arrocco_test::FakeTransport transport;
  NoAccount account;
  static LichessState state(transport, account);
  transport.answer(200, "{\"id\":\"tester\",\"username\":\"Tester\"}");
  state.client.begin();
  for (int i = 0; i < 3; ++i) state.poll();
  const int events = transport.openStreamId(0);
  transport.push(events,
                 "{\"type\":\"challenge\",\"challenge\":{\"id\":\"ours0001\",\"challenger\":{\"name\":\"tester\"},"
                 "\"destUser\":{\"name\":\"amico\"},\"variant\":{\"key\":\"standard\"},\"timeControl\":"
                 "{\"type\":\"clock\",\"limit\":600,\"increment\":0}}}\n");
  transport.push(events,
                 "{\"type\":\"challenge\",\"challenge\":{\"id\":\"them0001\",\"challenger\":{\"name\":\"Amico\"},"
                 "\"destUser\":{\"name\":\"Tester\"},\"variant\":{\"key\":\"standard\"},\"timeControl\":"
                 "{\"type\":\"clock\",\"limit\":600,\"increment\":0}}}\n");
  for (int i = 0; i < 3; ++i) state.poll();
  check(state.client.challengeCount() == 2, "the client lists both challenges");
  check(state.invitationCount() == 1, "only one is an invitation: ours is not (any case)");
  check(state.invitation(0) != nullptr && std::strcmp(state.invitation(0)->id, "them0001") == 0, "the one from Amico");
  check(state.invitation(1) == nullptr, "and nothing past it");
}

// ---- the keyboard ---------------------------------------------------------------------------------------------
void testKeyboard() {
  const char* chars = "1234567890qwertyuiopasdfghjkl-zxcvbnm_";
  bool everyKey = true;
  bool bigEnough = true;
  bool onScreen = true;
  for (const char* c = chars; *c != '\0'; ++c) {
    const Rect r = keyboard::keyRect(*c);
    const keyboard::Hit hit = keyboard::hitAt(r.cx(), r.cy());
    everyKey = everyKey && hit.key == keyboard::Key::Char && hit.ch == *c;
    // The corners belong to the key too: a fingertip lands anywhere on it.
    const keyboard::Hit corner = keyboard::hitAt(r.x, static_cast<int16_t>(r.y + r.h - 1));
    everyKey = everyKey && corner.key == keyboard::Key::Char && corner.ch == *c;
    bigEnough = bigEnough && r.w >= kMinButtonH && r.h >= kMinButtonH;
    onScreen = onScreen && r.x >= 0 && r.y >= 0 && r.x + r.w <= kScreenW && r.y + r.h <= kScreenH;
  }
  check(everyKey, "every key types its own character, edge to edge");
  check(bigEnough, "no key smaller than a fingertip button (48 px)");
  check(onScreen, "every key on the glass");
  check(keyboard::keyRect('A').w == 0 && keyboard::keyRect(' ').w == 0, "no key for what a username cannot have");
  const Rect q = keyboard::keyRect('q');
  const Rect w = keyboard::keyRect('w');
  check(keyboard::hitAt(static_cast<int16_t>(q.x + q.w + 2), q.cy()).key == keyboard::Key::None &&
            w.x - (q.x + q.w) == keyboard::kGap,
        "the 8 px between two keys types nothing");
  check(keyboard::hitAt(keyboard::deleteRect().cx(), keyboard::deleteRect().cy()).key == keyboard::Key::Delete,
        "Delete");
  check(keyboard::hitAt(keyboard::kCancel.cx(), keyboard::kCancel.cy()).key == keyboard::Key::Cancel &&
            keyboard::hitAt(keyboard::kClear.cx(), keyboard::kClear.cy()).key == keyboard::Key::Clear &&
            keyboard::hitAt(keyboard::kDone.cx(), keyboard::kDone.cy()).key == keyboard::Key::Done,
        "Cancel, Clear and Done");
  check(keyboard::kDone.y >= keyboard::deleteRect().y + keyboard::deleteRect().h + keyboard::kGap,
        "the buttons sit below the keys, a gap away");

  GFXcanvas1 canvas(kScreenW, kScreenH);
  canvas.fillScreen(arrocco::kWhite);
  keyboard::draw(canvas, "Name", "abcdefghijklmnopqrstuvwxyz0123", nullptr);
  const Rect field = keyboard::kField;
  check(inkIn(canvas, field.x + field.w - 8, field.y + 4, 6, field.h - 8) == 0,
        "thirty characters fit inside the field");
}

// ---- text --------------------------------------------------------------------------------------------------------
void testText() {
  check(waitTensOfSeconds(0) == 0 && waitTensOfSeconds(1) == 10 && waitTensOfSeconds(14999) == 10,
        "a wait under 15 s is 10 s, none is 0");
  check(waitTensOfSeconds(15000) == 20 && waitTensOfSeconds(61000) == 60 && waitTensOfSeconds(65000) == 70,
        "the nearest ten: Lichess's minute reads 60 s");

  GFXcanvas1 canvas(kScreenW, kScreenH);
  canvas.fillScreen(arrocco::kWhite);
  const int16_t x = kSideInnerX;
  const int16_t width = kSideInnerW;
  const int16_t next = drawWrapped(canvas, Font::Sans12, x, 100, width, 24,
                                   "No camera? On the USB serial console type token-set and a personal token with "
                                   "the board:play permission.");
  check(next > 100 + 2 * 24 && next <= 100 + 6 * 24, "a paragraph takes a few lines");
  check(inkIn(canvas, x + width, 0, kScreenW - x - width, kScreenH) == 0, "and never crosses the right edge");
  check(inkIn(canvas, 0, 0, x, kScreenH) == 0, "nor the left one");
  canvas.fillScreen(arrocco::kWhite);
  const int16_t long1 = drawWrapped(canvas, Font::Sans12, x, 100, 120, 24, "abcdefghijklmnopqrstuvwxyz0123456789");
  check(long1 > 100 + 24 && inkIn(canvas, x + 120, 0, kScreenW - x - 120, kScreenH) == 0,
        "a word longer than the line is cut, and goes on below");
  check(drawWrapped(canvas, Font::Sans12, x, 100, width, 24, "    ") == 100, "spaces alone draw nothing");
  const int16_t bottom = drawWrapped(canvas, Font::Sans12, x, 300, width, 24,
                                     "one two three four five six seven eight nine ten eleven twelve thirteen", 320);
  check(bottom <= 300 + 2 * 24, "nothing is drawn under the bottom it was given");

  char fitted[40];
  fitText(canvas, Font::Sans9, "a_rather_long_lichess_name_30", 120, fitted, sizeof fitted);
  check(textWidth(canvas, Font::Sans9, fitted) <= 120 && std::strstr(fitted, "...") != nullptr,
        "a long name is shortened to fit, with dots");
  fitText(canvas, Font::Sans9, "amico", 120, fitted, sizeof fitted);
  check(std::strcmp(fitted, "amico") == 0, "a short one is left alone");
  char tiny[4];
  check(formatText(tiny, sizeof tiny, "%s", "abcdef") == 6 && std::strcmp(tiny, "abc") == 0,
        "formatText cuts, terminates, and says how long it would have been");
}

// ---- QR codes ----------------------------------------------------------------------------------------------------------
FILE* g_expected = nullptr;
int g_pngs = 0;

void testQr(const char* dir, const char* text, int maxModulePx, const char* what) {
  static QrCode qr;   // 1 KB of buffers: static, as in the screen that owns one
  char line[160];
  const bool ok = qr.encode(text);
  std::snprintf(line, sizeof line, "%s: encoded", what);
  check(ok && qr.size() >= 21 && (qr.size() - 17) % 4 == 0, line);
  if (!ok) return;

  GFXcanvas1 canvas(kScreenW, kScreenH);
  canvas.fillScreen(arrocco::kWhite);
  // Something on every side of the box: the quiet zone must be painted white over it.
  canvas.fillScreen(arrocco::kBlack);
  canvas.fillRect(0, 0, kScreenW, kScreenH, arrocco::kWhite);
  const Rect box{kBoardX, kBoardY, kBoardPx, kBoardPx};
  canvas.fillRect(box.x, box.y, box.w, box.h, arrocco::kBlack);   // dirty: drawQr cleans its part
  const int px = drawQr(canvas, qr, box, maxModulePx);
  const int n = qr.size();
  const int expected = (box.w / (n + 2 * QrCode::kQuietModules)) < maxModulePx ? box.w / (n + 2 * QrCode::kQuietModules)
                                                                              : maxModulePx;
  std::snprintf(line, sizeof line, "%s: %d modules at %d px a module (1 px = 0.2 mm at 125 PPI)", what, n, px);
  check(px == expected && px >= 3, line);
  std::printf("  qr  %s\n", line);
  const int x0 = box.x + (box.w - n * px) / 2;
  const int y0 = box.y + (box.h - n * px) / 2;
  bool modules = true;
  for (int my = 0; my < n; ++my)
    for (int mx = 0; mx < n; ++mx)
      for (int corner = 0; corner < 4; ++corner) {
        const int sx = x0 + mx * px + ((corner & 1) ? px - 1 : 0);
        const int sy = y0 + my * px + ((corner & 2) ? px - 1 : 0);
        modules = modules && ink(canvas, sx, sy) == qr.module(mx, my);
      }
  std::snprintf(line, sizeof line, "%s: every module, corner to corner, as the encoder says", what);
  check(modules, line);
  const int quiet = QrCode::kQuietModules * px;
  const int ringInk = inkIn(canvas, x0 - quiet, y0 - quiet, n * px + 2 * quiet, quiet) +
                      inkIn(canvas, x0 - quiet, y0 + n * px, n * px + 2 * quiet, quiet) +
                      inkIn(canvas, x0 - quiet, y0, quiet, n * px) + inkIn(canvas, x0 + n * px, y0, quiet, n * px);
  std::snprintf(line, sizeof line, "%s: a white quiet zone of four modules around it", what);
  check(ringInk == 0, line);
  check(inkIn(canvas, box.x + box.w, 0, kScreenW - box.x - box.w, kScreenH) == 0,
        "nothing drawn right of the box");

  if (dir != nullptr && g_expected != nullptr) {
    char path[512];
    std::snprintf(path, sizeof path, "%s/qr_%02d.png", dir, ++g_pngs);
    check(writePng(canvas, path), "the PNG is written");
    std::fprintf(g_expected, "%s\n", text);
  }
}

void testQrCodes(const char* dir) {
  if (dir != nullptr) {
    char path[512];
    std::snprintf(path, sizeof path, "%s/qr_expected.txt", dir);
    g_expected = std::fopen(path, "w");
  }
  testQr(dir, "WIFI:T:nopass;S:Arrocco-4F2A;;", 12, "the setup network");
  testQr(dir, "http://192.168.100.200/login", 12, "the board's login address");
  // The whole lichess.org authorize URL, as long as it gets: the code it would need, for scale.
  testQr(dir,
         "https://lichess.org/oauth?response_type=code&client_id=arrocco.local&redirect_uri=http%3A%2F%2F192.168.100."
         "200%2Foauth%2Fcallback&code_challenge_method=S256&code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
         "&scope=board:play&state=0123456789abcdef0123456789abcdef",
         12, "a whole authorize URL");
  if (g_expected != nullptr) std::fclose(g_expected);

  static QrCode qr;
  char tooLong[400];
  std::memset(tooLong, 'x', sizeof tooLong - 1);
  tooLong[sizeof tooLong - 1] = '\0';
  check(!qr.encode(tooLong) && qr.size() == 0, "more than version 12 holds: refused, nothing encoded");
  GFXcanvas1 canvas(kScreenW, kScreenH);
  canvas.fillScreen(arrocco::kWhite);
  check(drawQr(canvas, qr, Rect{kBoardX, kBoardY, kBoardPx, kBoardPx}) == 0 &&
            inkIn(canvas, 0, 0, kScreenW, kScreenH) == 0,
        "and nothing is drawn");
  check(!qr.encode("") && !qr.encode(nullptr), "nothing to encode is refused too");
}

}  // namespace

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : nullptr;
  testUsernames();
  testRecentFriends();
  testFriendsInFlash();
  testInvitations();
  testKeyboard();
  testText();
  testQrCodes(dir);
  std::printf("lichess screens: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
