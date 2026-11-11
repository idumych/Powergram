// SPDX-License-Identifier: GPL-3.0-or-later
// What a file to be sent is, as far as Telegram cares: a photo Haiku can
// read (and its size), a video Telegram plays (its size and length, from the
// Media Kit), or a document.
#pragma once

#include "../core/ChatHistory.h"

// Fills in kind, width, height and duration; path is set from `path`.
pp::OutgoingFile ProbeFile(const char* path);
