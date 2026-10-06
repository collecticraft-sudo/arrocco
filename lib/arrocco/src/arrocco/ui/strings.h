// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — every user-visible string in one place (English only).
#pragma once

namespace arrocco::ui::str {

// Menu
constexpr char kAppTitle[] = "Arrocco";
constexpr char kAppSubtitle[] = "e-ink chess";
constexpr char kMenuResume[] = "Resume game";
// Under "Resume game", joined by a drawn middle dot: "vs engine · move 12".
constexpr char kResumeVsEngine[] = "vs engine";
constexpr char kResumeTwoPlayers[] = "two players";
constexpr char kResumeMoveFmt[] = "move %d";
constexpr char kMenuTwoPlayers[] = "Two players";
constexpr char kMenuEngine[] = "Play vs engine";
constexpr char kMenuPuzzles[] = "Puzzles";
constexpr char kMenuLichess[] = "Lichess";
constexpr char kMenuSettings[] = "Settings";
constexpr char kComingSoon[] = "coming soon";
constexpr char kBatteryFmt[] = "Battery %d%%";        // nothing at all without a gauge
constexpr char kUsbPowered[] = "USB power";

// Play vs engine — setup screen
constexpr char kEngineTitle[] = "Play vs engine";
constexpr char kEngineSideWhite[] = "You play White";
constexpr char kEngineSideBlack[] = "You play Black";
constexpr char kEngineSideRandom[] = "Colour drawn at the start";
constexpr char kEngineLevelFmt[] = "Level %d: %s";
constexpr char kEngineStart[] = "Choose the clock and start";
constexpr char kEngineHint[] = "Tap a line to change it";
constexpr char kEngineMissing[] = "No engine in this build";

// Engine level names. What strength each one stands for is in arrocco/engine.h.
constexpr char kLevel1[] = "Beginner";
constexpr char kLevel2[] = "Very easy";
constexpr char kLevel3[] = "Easy";
constexpr char kLevel4[] = "Casual";
constexpr char kLevel5[] = "Club";
constexpr char kLevel6[] = "Strong";
constexpr char kLevel7[] = "Tough";
constexpr char kLevel8[] = "Expert";

// Engine, during the game
constexpr char kEngineThinking[] = "Thinking...";
constexpr char kEngineLevelLineFmt[] = "Level %d: %s";   // under "Thinking..."

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
constexpr char kButtonResign[] = "Resign";           // against the engine: no draw to agree
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

// New game popup: before an unfinished game is replaced (game screen, clock picker)
constexpr char kNewGameTitle[] = "Start a new game?";
constexpr char kNewGameLost[] = "The game in progress will be lost";

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

// Sleep screen: the board is in deep sleep and any touch wakes it (sleep_screen.h)
constexpr char kSleepTapToWake[] = "Tap to wake";
constexpr char kSleepNote[] = "Asleep. Tap to wake.";             // when the build has no picture
constexpr char kSleepArtCredit[] = "Van Gogh, The Starry Night, 1889";  // names sleep_art.h

// Puzzles: the level picker (the first visit, and the "Level" button)
constexpr char kPuzzleTitle[] = "Puzzles";
constexpr char kPuzzlePickFirst[] = "How well do you play? The level then follows your results";
constexpr char kPuzzlePickAgainFmt[] = "Your rating: %d after %d puzzles. A level starts it again";
constexpr char kPuzzleLevel1[] = "Beginner";
constexpr char kPuzzleLevel2[] = "Casual player";
constexpr char kPuzzleLevel3[] = "Club player";
constexpr char kPuzzleLevel4[] = "Strong club player";
constexpr char kPuzzleLevelNoteFmt[] = "start at %d";          // under each level's name
constexpr char kPuzzleCredit[] = "Puzzles from the Lichess puzzle database, CC0";

// Puzzles: the board's side column
constexpr char kPuzzleNew[] = "New puzzle";                     // until the opponent's move
constexpr char kPuzzleAboutToMoveFmt[] = "%s is about to move"; // "Black is about to move"
constexpr char kPuzzlePlayedFmt[] = "%s played %s";             // "Black played Qxd4"
constexpr char kPuzzleCorrect[] = "Correct!";
constexpr char kPuzzleYouPlayedFmt[] = "You played %s";
constexpr char kPuzzleWrong[] = "Not the move";
constexpr char kPuzzleTryAgainFmt[] = "%s? Try again";          // "Nf3? Try again"
constexpr char kPuzzleHintLine[] = "Hint: move this piece";
constexpr char kPuzzleSolutionHead[] = "Solution";
constexpr char kPuzzleSolutionFmt[] = "%s plays %s";            // "White plays Qxf7+"
constexpr char kPuzzleSolutionEnd[] = "That was the whole line";
constexpr char kPuzzleSolved[] = "Solved!";
constexpr char kPuzzleSolvedPlain[] = "Solved";                 // after a wrong try or with help
constexpr char kPuzzleSolvedMate[] = "Checkmate!";
constexpr char kPuzzleSolvedWell[] = "Well played";
constexpr char kPuzzleSolvedHint[] = "With a hint";
constexpr char kPuzzleSolvedLate[] = "After a wrong try";
constexpr char kPuzzleSolvedHelped[] = "With the solution's help";
constexpr char kPuzzleSolvedPractice[] = "Practice: no rating change";
constexpr char kPuzzleRatingFmt[] = "Puzzle rating %d";
constexpr char kPuzzleIdFmt[] = "lichess.org/training/%s";
constexpr char kPuzzleYourRating[] = "Your rating";
constexpr char kPuzzleStatsFmt[] = "Solved %d of %d";
constexpr char kPuzzleStreakFmt[] = ", streak %d";            // from 2 in a row
constexpr char kPuzzleStatsNone[] = "No puzzle scored yet";
constexpr char kPuzzleProvisional[] = "Still finding your level";
constexpr char kButtonHint[] = "Hint";
constexpr char kButtonSolution[] = "Solution";
constexpr char kButtonSkip[] = "Skip";
constexpr char kButtonNextPuzzle[] = "Next";
constexpr char kButtonRetry[] = "Retry";
constexpr char kButtonLevel[] = "Level";

}  // namespace arrocco::ui::str
