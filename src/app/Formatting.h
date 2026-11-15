// SPDX-License-Identifier: GPL-3.0-or-later
// What the main window's message box sends, formatted: the Markdown in it
// read by TDLib (synchronously, td_execute), as the official apps read it,
// with mentions picked from the suggestions kept; and a message to edit,
// turned back into that Markdown for the box.
#pragma once

#include <Message.h>

#include "../core/Markdown.h"

// Typed text as it goes out: a formattedText with the Markdown read and
// `mentions` kept; as typed if TDLib can't read it.
pp::json TypedText(const std::string& text, const pp::Mentions& mentions);

// The mentions in a kMsgSendText or kMsgEditText ("mention_name",
// "mention_user").
pp::Mentions MessageMentions(const BMessage* message);

// The sendMessage for the main window's kMsgSendText.
pp::json OutgoingText(const BMessage* sendText);
// The edit for its kMsgEditText; `media`: the message is a photo, video or
// file, whose caption is edited, kept above it if `captionAbove`.
pp::json OutgoingEdit(const BMessage* editText, bool media, bool captionAbove);

// kMsgEditDraft for the main window: TDLib's `message` as the box shows it
// for editing (Markdown, and mentions by name).
BMessage EditDraftMessage(const pp::json& message);
