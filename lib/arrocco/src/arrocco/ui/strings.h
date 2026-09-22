// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — every user-visible string in one place (English only).
#pragma once

namespace arrocco::ui::str {

// Menu
constexpr char kAppTitle[] = "Arrocco";
constexpr char kAppSubtitle[] = "e-ink chess";
constexpr char kMenuResume[] = "Resume game";
constexpr char kMenuTwoPlayers[] = "Two players";
constexpr char kMenuEngine[] = "Play vs engine";
constexpr char kMenuPuzzles[] = "Puzzles";
constexpr char kMenuLichess[] = "Lichess";
constexpr char kMenuSettings[] = "Settings";
constexpr char kComingSoon[] = "coming soon";
constexpr char kBatteryFmt[] = "Battery %d%%";
constexpr char kBatteryNoGauge[] = "Battery: no gauge";
constexpr char kUsbPowered[] = "USB power";

// Clock picker
constexpr char kClockTitle[] = "Game clock";
constexpr char kClockOff[] = "No clock";
constexpr char kClock5[] = "5 + 0";
constexpr char kClock10[] = "10 + 0";
constexpr char kClock15Inc10[] = "15 + 10";
constexpr char kClock30[] = "30 + 0";
constexpr char kBack[] = "Back";

// Settings
constexpr char kSettingsTitle[] = "Settings";
constexpr char kSettingFlipOn[] = "Flip board by default: on";
constexpr char kSettingFlipOff[] = "Flip board by default: off";
constexpr char kSettingSoundOn[] = "Sound: on";
constexpr char kSettingSoundOff[] = "Sound: off";
constexpr char kSettingRefreshNormal[] = "Refresh: normal";
constexpr char kSettingRefreshFewer[] = "Refresh: fewer flashes";

// Game side panel
constexpr char kWhiteToMove[] = "White to move";
constexpr char kBlackToMove[] = "Black to move";
constexpr char kCheck[] = "Check!";
constexpr char kNoMovesYet[] = "Tap a piece to start";
constexpr char kWhiteLabel[] = "White";
constexpr char kBlackLabel[] = "Black";
constexpr char kMaterialEven[] = "Material: even";
constexpr char kMaterialFmt[] = "Material: %s +%d";
constexpr char kButtonNewGame[] = "New game";
constexpr char kButtonUndo[] = "Undo";
constexpr char kButtonFlip[] = "Flip";
constexpr char kButtonResignDraw[] = "Resign/Draw";
constexpr char kButtonMenu[] = "Menu";
constexpr char kButtonPrev[] = "<";
constexpr char kButtonNext[] = ">";
constexpr char kButtonResult[] = "Result";
constexpr char kReviewFmt[] = "%s  (%d/%d)";       // "4... Nf6  (8/31)": shown move, ply of plies
constexpr char kReviewStartFmt[] = "Start  (0/%d)";

// Promotion popup
constexpr char kPromoteTitle[] = "Promote to";

// Resign / draw popup
constexpr char kEndGameTitle[] = "End the game?";
constexpr char kWhiteResigns[] = "White resigns";
constexpr char kBlackResigns[] = "Black resigns";
constexpr char kAgreeDraw[] = "Draw by agreement";
constexpr char kCancel[] = "Cancel";

// Game over
constexpr char kWhiteWins[] = "White wins";
constexpr char kBlackWins[] = "Black wins";
constexpr char kDraw[] = "Draw";
constexpr char kByCheckmate[] = "Checkmate";
constexpr char kByStalemate[] = "Stalemate";
constexpr char kByFiftyMove[] = "Fifty-move rule";
constexpr char kByRepetition[] = "Threefold repetition";
constexpr char kByMaterial[] = "Insufficient material";
constexpr char kByResignation[] = "Resignation";
constexpr char kByTimeout[] = "Time out";
constexpr char kByAgreement[] = "Agreement";
constexpr char kGameOverReview[] = "Review";

}  // namespace arrocco::ui::str
