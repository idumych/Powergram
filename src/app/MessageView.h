// SPDX-License-Identifier: GPL-3.0-or-later
// The open chat's messages as bubbles, like Paper Plane's chat history:
// incoming on the left with the sender's name, outgoing on the right.
// Lays out word-wrapped text itself so it reflows when the window resizes.
#pragma once

#include <String.h>
#include <View.h>

#include <list>
#include <map>
#include <set>

#include <string>
#include <unordered_map>
#include <vector>

#include "../core/Format.h"

struct MessageRow {
	int64 id;
	BString sender;     // empty for outgoing and private chats
	BString text;
	BString time;
	bool outgoing;
	bool read;          // outgoing and read by the other side
	bool pending;
	bool failed;
	bool edited;
	bool service;       // also date separators (id 0)
	BString forward;    // "Forwarded from …", or empty
	BString replySender;
	BString replyText;  // empty without a reply

	// pp::Media::Kind: 0 none, 1 photo, 2 sticker, 3 video, 4 GIF,
	// 5 round video message.
	int32 mediaKind;
	int32 duration;             // videos, seconds
	BString imagePath;          // downloaded image, or empty for now
	std::string minithumbnail;  // tiny JPEG, drawn blurred meanwhile
	int32 mediaWidth;
	int32 mediaHeight;
	bool spoiler;               // hidden until clicked
	bool secret;                // self-destructing: hidden until opened
	BString emoji;              // a sticker without an image

	// Groups: the sender's picture beside their incoming messages, drawn
	// once per run of messages from the same sender.
	bool showAvatar;
	int64 senderKey;            // runs are messages with the same key
	BString avatarPath;         // downloaded picture, or empty
	std::string avatarPreview;  // its tiny JPEG meanwhile
	BString avatarInitials;     // without any picture

	std::vector<pp::Span> spans; // formatting and links in `text`

	struct Reaction {
		BString emoji;
		int32 count;
		bool chosen;
	};
	std::vector<Reaction> reactions;
};

class BBitmap;

class MessageView : public BView {
public:
	MessageView();

	// scrollTo: a message to show at the top (the first unread one).
	void SetMessages(const std::vector<MessageRow>& rows, bool scrollToEnd,
		int64 scrollTo = 0);
	// The jump-to-latest button: whether newer messages aren't loaded, and
	// the chat's unread count for its badge.
	void SetMoreBelow(bool moreBelow);
	// Text size relative to the system font (1.0 = the same).
	void SetTextScale(float scale);
	// Outlines a message (the search result on screen); 0 for none.
	void SetHighlight(int64 id);
	void SetUnreadCount(int32 count);
	void SetEmptyText(const char* text);

	void Draw(BRect updateRect) override;
	void FrameResized(float width, float height) override;
	void AttachedToWindow() override;
	void ScrollTo(BPoint where) override;
	void MouseDown(BPoint where) override;
	void DetachedFromWindow() override;
	void MessageReceived(BMessage* message) override;
	void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;

	const MessageRow* Row(int64 id) const;
	// Scrolls so that message `id` is at the top (tests, jump to message).
	void ScrollToMessage(int64 id);
	// The last message visible at the bottom and its distance from the
	// bottom edge (0 when at the end); for tests.
	int64 BottomMessage(float* fromBottom, bool* atEnd) const;

private:
	struct Layout {
		BRect bubble;
		std::vector<BString> lines;
		float textTop;
		float forwardTop;   // baseline area of the "Forwarded from" line
		float replyTop;     // top of the reply block
		BRect media;        // where the photo or sticker goes
		std::vector<int32> lineChars;  // first character of each line
		struct Link {
			BRect frame;
			std::string url;
		};
		std::vector<Link> links;       // clickable, in view coordinates
		std::vector<BRect> reactions;  // a pill per reaction, view coordinates
	};

	// Character widths of a string, measured once with one app_server call
	// (GetEscapements) and cached: StringWidth per candidate line cost
	// 1.8 s for 300 messages and stalled the whole desktop.
	struct Measured {
		std::vector<int32> offsets;   // byte offset of each character, + end
		std::vector<float> widths;    // width of each character
		float total = 0;
	};

	// Where the reader is, kept across re-layouts: at the end, or the last
	// message visible at the bottom and its distance from the bottom edge.
	// Chats stay anchored at the bottom, like Telegram's.
	struct Anchor {
		bool atEnd = true;
		int64 id = 0;
		float fromBottom = 0;
	};
	Anchor _SaveAnchor(float viewBottom) const;
	void _RestoreAnchor(const Anchor& anchor);

	BBitmap* _Bitmap(const MessageRow& row, bool* blurred, bool round = false);
	void _DrawMedia(const MessageRow& row, const Layout& layout, rgb_color fg,
		float line);
	void _DrawAvatar(const MessageRow& row, BRect circle);
	BBitmap* _AvatarBitmap(const MessageRow& row);
	void _ClearBitmaps();

	void _Relayout();
	void _UpdateScrollBar();
	const Measured& _Measure(const BString& text, bool bold);
	float _Width(const BString& text, bool bold = false)
		{ return _Measure(text, bold).total; }
	// Character widths of a formatted message (each run in its own font).
	const Measured& _MeasureRow(const MessageRow& row);
	std::vector<BString> _Wrap(const BString& text, const Measured& m, float width,
		float* widest, std::vector<int32>* lineChars);
	void _FontFor(uint32 style, BFont* font) const;
	void _DrawText(const MessageRow& row, const Layout& l, float x, float line,
		rgb_color fg, rgb_color fill);

	std::vector<MessageRow> fRows;
	std::vector<Layout> fLayout;
	float fContentHeight;
	BString fEmptyText;
	std::unordered_map<std::string, Measured> fMeasured;
	// Decoded images by path ("mini:<id>" for inline previews), most
	// recently used first, at most kMaxBitmaps.
	std::map<std::string, BBitmap*> fBitmaps;
	std::list<std::string> fBitmapOrder;
	std::set<int64> fRevealed;  // spoilers clicked open
	bool fDropTarget;           // files are being dragged over us
	bool fMoreBelow = false;
	int64 fHighlight = 0;
	BFont fPlain;     // the system fonts at the chosen text size
	BFont fBold;
	BFont fFixed;
	int32 fUnreadCount = 0;
	BRect _JumpButtonFrame() const;
	bool fAtEnd;
	bool fRelayouting;    // our own scrolling: not the reader's
	float fLastHeight;    // height at the last layout, for FrameResized
};
