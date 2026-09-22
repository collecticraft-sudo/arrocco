# CT800 V1.46 — import unmodified

Source: https://www.ct800.net/downloads/ct800-v1.46.zip (SHA-256 below), downloaded on 22 September 2026.
Author: Rasmus Althoff <info@ct800.net>, based on NG-Play by George Georgopoulos. Licence: GPL-3.0-or-later (see COPYING.txt).

This directory holds the `source/application-uci` tree of the release exactly as shipped, plus the licence, authors, readme and changelog files. Nothing here is edited: every Arrocco change lives in `lib/ct800/port/` so that a future upstream release can be dropped in and the diff stays auditable.

Not imported: `binaries/`, `documentation/`, `tools/`, `source/application` (the STM32 bare-metal firmware) and `source/tool_bin`, because Arrocco does not use them.

SHA-256 of ct800-v1.46.zip: 61647c7146fd8b3219c36dc987130d92be9f0fe37d494bb41aee192c930aa1c6
