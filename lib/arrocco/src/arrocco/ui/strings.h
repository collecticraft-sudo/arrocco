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


// ---- Lichess (lichess_screen.h, lichess_game_screen.h) -------------------------------------
constexpr char kLichessInGame[] = "game in progress";          // the menu's note under "Lichess"
constexpr char kLichessTitle[] = "Lichess";

// Not on Wi-Fi: the board's setup network, with a QR code the phone camera can join
constexpr char kWifiTitle[] = "Connect to Wi-Fi";
constexpr char kWifiStep1[] = "1. On your phone, join this network (or scan the code):";
constexpr char kWifiStep2[] = "2. Pick your home Wi-Fi on the page that opens.";
constexpr char kWifiNoPageFmt[] = "No page? Open %s";
constexpr char kWifiWaitingPhone[] = "Waiting for your phone";
constexpr char kWifiJoining[] = "Connecting to Wi-Fi";
constexpr char kWifiFailed[] = "Cannot connect to Wi-Fi";
constexpr char kWifiTryAgain[] = "Try again";
constexpr char kWifiSetUp[] = "Set up Wi-Fi";

// No account: the QR code of the login
constexpr char kLinkTitle[] = "Link Lichess";
constexpr char kLinkStep1[] = "1. Scan the code with your phone, on the same Wi-Fi as the board.";
constexpr char kLinkStep2[] = "2. Sign in to Lichess and tap Authorize.";
constexpr char kLinkWaiting[] = "Waiting for your phone";
constexpr char kLinkStarting[] = "Preparing the code";
constexpr char kLinkExchanging[] = "Linking the account";
constexpr char kLinkFailed[] = "The login did not work";
constexpr char kLinkOrOpenFmt[] = "Or open %s on the phone.";
constexpr char kLinkTokenHint[] = "No camera? On the USB serial console type token-set and a personal token "
                                  "with the board:play permission.";
constexpr char kLinkNewCode[] = "New code";

// Linked: the Lichess menu
constexpr char kHubSignedInFmt[] = "Signed in as %s";
constexpr char kHubConnecting[] = "Connecting to Lichess";
constexpr char kHubRefused[] = "Lichess refused the link: link the account again";
constexpr char kHubOfflineFmt[] = "Cannot reach Lichess: %s";
constexpr char kHubWaitFmt[] = "Lichess asks to wait: %d s";
constexpr char kHubPlayComputer[] = "Play the computer";
constexpr char kHubComputerNoteFmt[] = "level %d%cclock %s";       // kNoteDot between the two
constexpr char kHubChallenge[] = "Challenge a friend";
constexpr char kHubInvitations[] = "Invitations";
constexpr char kHubInvitationsNone[] = "none";
constexpr char kHubInvitationsFmt[] = "%d waiting";
constexpr char kHubRatedOff[] = "Rated games: off";
constexpr char kHubRatedOn[] = "Rated games: on";
constexpr char kHubRatedNote[] = "friends only: the computer is always casual";
constexpr char kHubUnlink[] = "Unlink account";
constexpr char kHubLinkAgain[] = "Link again";
constexpr char kHubTryAgain[] = "Try again";
constexpr char kHubBackToGame[] = "Back to the game";
constexpr char kHubVsFmt[] = "vs %s%cmove %d";                    // "vs amico · move 12"
constexpr char kUnlinkTitle[] = "Unlink the account?";
constexpr char kUnlinkSubtitle[] = "The board forgets its Lichess token";
constexpr char kUnlinkYes[] = "Unlink";

// Play the computer
constexpr char kComputerTitle[] = "Play the computer";
constexpr char kComputerHint[] = "Lichess Stockfish. Always casual. Tap a line to change it";
constexpr char kComputerLevelFmt[] = "Stockfish level %d";
constexpr char kComputerWhite[] = "You play White";
constexpr char kComputerBlack[] = "You play Black";
constexpr char kComputerRandom[] = "Colour drawn by Lichess";
constexpr char kClockFmt[] = "Clock: %s";
constexpr char kComputerStart[] = "Start the game";
constexpr char kLichessClock5[] = "5 + 0";
constexpr char kLichessClock10[] = "10 + 0";
constexpr char kLichessClock15[] = "15 + 10";
constexpr char kLichessClock30[] = "30 + 0";

// Challenge a friend
constexpr char kFriendTitle[] = "Challenge a friend";
constexpr char kFriendHint[] = "Tap a line to change it";
constexpr char kFriendNameFmt[] = "Friend: %s";
constexpr char kFriendChoose[] = "Choose a friend";
constexpr char kFriendCasual[] = "Casual game";
constexpr char kFriendRated[] = "Rated game";
constexpr char kFriendSend[] = "Send the challenge";
constexpr char kNamesTitle[] = "Choose a friend";
constexpr char kNamesHint[] = "Recent opponents";
constexpr char kNamesNone[] = "Nobody yet: type the name";
constexpr char kNamesType[] = "Type a name";
constexpr char kKeyboardTitle[] = "Your friend's Lichess name";
constexpr char kKeyboardDelete[] = "Delete";
constexpr char kKeyboardClear[] = "Clear";
constexpr char kKeyboardDone[] = "Done";
constexpr char kKeyboardTooShort[] = "A Lichess name has at least 2 letters";

// Invitations
constexpr char kInvitesTitle[] = "Invitations";
constexpr char kInvitesHint[] = "Challenges sent to you on Lichess";
constexpr char kInvitesNone[] = "No invitations right now";
constexpr char kInvitesAccept[] = "Accept";
constexpr char kInvitesDecline[] = "Decline";
constexpr char kInvitesRated[] = "rated";
constexpr char kInvitesCasual[] = "casual";
constexpr char kInvitesNoClock[] = "no clock";
constexpr char kInvitesDaysFmt[] = "%d days a move";
constexpr char kInvitesNotHere[] = "not playable on the board";

// Waiting for a game to start
constexpr char kStarting[] = "Starting the game...";              // under the page, while Lichess makes it
constexpr char kWaitingAskTitle[] = "Challenge sent";
constexpr char kWaitingForFmt[] = "Waiting for %s to accept";
constexpr char kWaitingCancel[] = "Cancel the challenge";
constexpr char kNotStarted[] = "Lichess did not start the game";

// The online game
constexpr char kOnlineYourMove[] = "Your move";
constexpr char kOnlineTheirMove[] = "Their move";
constexpr char kOnlineToMoveFmt[] = "%s to move";
constexpr char kOnlineYou[] = "You";
constexpr char kOnlineStockfishFmt[] = "Stockfish %d";
constexpr char kOnlineSendingFmt[] = "Sending %s...";
constexpr char kOnlineWaitFmt[] = "%s waits %d s (Lichess)";
constexpr char kOnlineRefusedFmt[] = "Refused: %s";
constexpr char kOnlineReconnecting[] = "Reconnecting...";
constexpr char kOnlineDrawToUs[] = "Draw offered to you";
constexpr char kOnlineDrawByUs[] = "You offered a draw";
constexpr char kOnlineGoneFmt[] = "Opponent left: claim in %d s";
constexpr char kOnlineGoneNow[] = "Opponent left: claim the win";
constexpr char kOnlineResign[] = "Resign";
constexpr char kOnlineAbort[] = "Abort";
constexpr char kOnlineOfferDraw[] = "Offer draw";
constexpr char kOnlineAcceptDraw[] = "Accept draw";
constexpr char kOnlineDeclineDraw[] = "Decline draw";
constexpr char kOnlineClaimWin[] = "Claim win";
constexpr char kOnlineResignTitle[] = "Resign the game?";
constexpr char kOnlineAbortTitle[] = "Abort the game?";
constexpr char kOnlineEndsForGood[] = "Lichess ends it for good";
constexpr char kOnlineYouWon[] = "You won";
constexpr char kOnlineYouLost[] = "You lost";
constexpr char kOnlineAborted[] = "Game aborted";
constexpr char kOnlineByAbort[] = "Nobody moved twice";
constexpr char kOnlineByGone[] = "The opponent left";
constexpr char kOnlineByLichess[] = "Ended by Lichess";

}  // namespace arrocco::ui::str
