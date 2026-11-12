# Powergram, a Telegram client for Haiku.
#   make          build the app (on Haiku/Renku)
#   make test     portable core tests (anywhere with a C++17 compiler)

NAME := Powergram
CXX ?= g++
override CXXFLAGS += -std=c++17 -Wall -Wextra -O2

CORE_SRCS := src/core/AuthFlow.cpp src/core/ChatList.cpp src/core/ChatHistory.cpp \
	src/core/SendQueue.cpp src/core/Files.cpp src/core/Stickers.cpp \
	src/core/Format.cpp src/core/Presence.cpp src/core/ChatInfo.cpp \
	src/core/Links.cpp src/core/Pinned.cpp src/core/VoiceNote.cpp src/core/Mentions.cpp \
	src/core/Profile.cpp src/core/QrCode.cpp src/core/SecretChat.cpp src/core/Contacts.cpp src/core/Compose.cpp src/core/Lottie.cpp src/core/Webm.cpp \
	src/third_party/qrcodegen/qrcodegen.cpp
APP_SRCS := src/app/App.cpp src/app/TdClient.cpp src/app/Settings.cpp \
	src/app/LoginWindow.cpp src/app/MainWindow.cpp src/app/MessageView.cpp \
	src/app/BitmapUtils.cpp src/app/Icons.cpp src/app/Pickers.cpp src/app/ChatPicker.cpp \
	src/app/SettingsWindow.cpp src/app/VoicePlayer.cpp \
	src/app/ChatInfoWindow.cpp src/app/FileSaving.cpp src/app/OpenLinkWindow.cpp \
	src/app/VoiceRecorder.cpp src/app/MentionPopup.cpp src/app/MediaProbe.cpp \
	src/app/AvatarView.cpp src/app/ProfileWindow.cpp src/app/NewChatWindow.cpp src/app/ComposeWindows.cpp \
	src/app/MediaWindow.cpp src/app/VideoPlayback.cpp src/app/LottieRenderer.cpp \
	src/app/StickerPlayer.cpp src/app/WebmPlayer.cpp

BUILD := obj
CORE_OBJS := $(CORE_SRCS:%.cpp=$(BUILD)/%.o)
APP_OBJS := $(APP_SRCS:%.cpp=$(BUILD)/%.o)

# TDLib from HaikuPorts ships static libraries only; this is the order its
# CMake targets give (TdStaticTargets.cmake: Td::TdJsonStatic).
TD_LIBS := -ltdjson_static -ltdjson_private -ltdclient -ltdcore -ltdapi \
	-ltdmtproto -ltdnet -ltddb -ltdsqlite -ltde2e -ltdactor -ltdutils \
	-lssl -lcrypto -lz -lnetwork

MIMESET := mimeset
MEDIA_LIBS := -lmedia -lopus -logg -lvpx
ifneq ($(HAIKU_CROSS),)
  # Haiku arm64, cross-compiled on Linux (tools/haiku-arm64: . env.sh, then
  # build-deps.sh for TDLib, OpenSSL and zlib, all static). Haiku arm64 has
  # no Media Kit yet: stand-ins replace the sound and video parts.
  # Haiku's host mimeset can't read arm64 binaries; the package sets the type.
  override CXXFLAGS += $(HAIKU_CROSS_CFLAGS) -I$(HAIKU_ARM64)/deps/include
  override LDFLAGS += $(HAIKU_CROSS_LDFLAGS) -L$(HAIKU_ARM64)/deps/lib
  MEDIA_SRCS := src/app/VoicePlayer.cpp src/app/VoiceRecorder.cpp \
	src/app/MediaProbe.cpp src/app/VideoPlayback.cpp src/app/WebmPlayer.cpp
  APP_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(filter-out $(MEDIA_SRCS),$(APP_SRCS)) \
	src/app/NoMediaKit.cpp)
  MEDIA_LIBS :=
  MIMESET := :
endif

ifneq ($(HAIKU_CROSS)$(filter Haiku,$(shell uname -s)),)
all: $(NAME) $(BUILD)/PowergramDeskbar

# The Deskbar item: an add-on of its own, so Deskbar never loads the app.
$(BUILD)/PowergramDeskbar: src/deskbar/DeskbarItem.cpp src/app/Messages.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ src/deskbar/DeskbarItem.cpp $(LDFLAGS) -lbe

# The app at ./Powergram, resources from obj/ (packaging/build-hpkg.sh).
$(NAME): $(CORE_OBJS) $(APP_OBJS) $(BUILD)/$(NAME).rsrc
	$(CXX) -o $@ $(CORE_OBJS) $(APP_OBJS) $(LDFLAGS) -lbe -ltracker -ltranslation -llocalestub $(MEDIA_LIBS) $(TD_LIBS)
	xres -o $@ $(BUILD)/$(NAME).rsrc
	$(MIMESET) -f $@

# The real main window with made-up chats, for screenshots.
MOCKUP_OBJS := $(filter-out $(BUILD)/src/app/App.o $(BUILD)/src/app/TdClient.o \
	$(BUILD)/src/app/Settings.o, $(APP_OBJS))
mockup: $(BUILD)/PowergramMockup
$(BUILD)/PowergramMockup: $(BUILD)/tests/mockup.o $(CORE_OBJS) $(MOCKUP_OBJS)
	$(CXX) -o $@ $^ -lbe -lmedia -ltracker -ltranslation -llocalestub -lopus -logg -lvpx -lz

# Frames of an animated sticker (.tgs, Lottie .json or .webm) drawn into a PNG.
lottie-render: $(BUILD)/lottie_render
$(BUILD)/lottie_render: $(BUILD)/tests/lottie_render.o $(BUILD)/src/app/LottieRenderer.o \
		$(BUILD)/src/app/StickerPlayer.o $(BUILD)/src/app/WebmPlayer.o \
		$(BUILD)/src/core/Lottie.o $(BUILD)/src/core/Webm.o
	$(CXX) -o $@ $^ -lbe -ltranslation -lvpx -lz

# The entry's icon buttons and a picker with made-up content.
pickers-demo: $(BUILD)/pickers_demo
$(BUILD)/pickers_demo: $(BUILD)/tests/pickers_demo.o $(BUILD)/src/app/Pickers.o \
		$(BUILD)/src/app/Icons.o $(BUILD)/src/core/Stickers.o $(BUILD)/src/app/StickerPlayer.o \
		$(BUILD)/src/app/WebmPlayer.o $(BUILD)/src/app/LottieRenderer.o \
		$(BUILD)/src/core/Lottie.o $(BUILD)/src/core/Webm.o
	$(CXX) -o $@ $^ -lbe -ltranslation -lvpx -lz

# Scroll position through real resizes, in a visible window.
live-scroll: $(BUILD)/live_scroll
$(BUILD)/live_scroll: $(BUILD)/tests/live_scroll.o $(BUILD)/src/app/MessageView.o \
		$(BUILD)/src/app/BitmapUtils.o $(BUILD)/src/core/Format.o
	$(CXX) -o $@ $^ -lbe -ltranslation

# Saving received files: copying, and the file type the copy gets.
file-saving-test: $(BUILD)/file_saving_test
	$(BUILD)/file_saving_test
$(BUILD)/file_saving_test: $(BUILD)/tests/file_saving_test.o $(BUILD)/src/app/FileSaving.o
	$(CXX) -o $@ $^ -lbe

# Recording voice messages: the OGG/Opus file, read back by the Media Kit.
voice-record-test: $(BUILD)/voice_record_test
	$(BUILD)/voice_record_test
$(BUILD)/voice_record_test: $(BUILD)/tests/voice_record_test.o $(BUILD)/src/app/VoiceRecorder.o \
		$(BUILD)/src/core/VoiceNote.o
	$(CXX) -o $@ $^ -lbe -lmedia -lopus -logg

# Two seconds from the real sound input (needs one; not part of make test).
voice-record-live: $(BUILD)/voice_record_live
$(BUILD)/voice_record_live: $(BUILD)/tests/voice_record_live.o $(BUILD)/src/app/VoiceRecorder.o \
		$(BUILD)/src/core/VoiceNote.o
	$(CXX) -o $@ $^ -lbe -lmedia -lopus -logg

# What files to be sent are: photos, videos (size and length), documents.
media-probe-test: $(BUILD)/media_probe_test
	$(BUILD)/media_probe_test tests/data
$(BUILD)/media_probe_test: $(BUILD)/tests/media_probe_test.o $(BUILD)/src/app/MediaProbe.o \
		$(BUILD)/src/core/ChatHistory.o $(BUILD)/src/core/ChatList.o \
		$(BUILD)/src/core/Format.o
	$(CXX) -o $@ $^ -lbe -lmedia -ltranslation

# The media viewer's video playback: frames, clock, pause, seek, end, loop.
video-playback-test: $(BUILD)/video_playback_test
	$(BUILD)/video_playback_test tests/data
$(BUILD)/video_playback_test: $(BUILD)/tests/video_playback_test.o \
		$(BUILD)/src/app/VideoPlayback.o
	$(CXX) -o $@ $^ -lbe -lmedia

# The media viewer window with a file, to look at.
media-viewer-demo: $(BUILD)/media_viewer_demo
$(BUILD)/media_viewer_demo: $(BUILD)/tests/media_viewer_demo.o $(BUILD)/src/app/MediaWindow.o \
		$(BUILD)/src/app/VideoPlayback.o $(BUILD)/src/core/ChatHistory.o \
		$(BUILD)/src/core/ChatList.o $(BUILD)/src/core/Format.o
	$(CXX) -o $@ $^ -lbe -lmedia -ltranslation

# The Deskbar item with a made-up count (stands in for the app).
deskbar-demo: $(BUILD)/deskbar_demo $(BUILD)/PowergramDeskbar
$(BUILD)/deskbar_demo: $(BUILD)/tests/deskbar_demo.o
	$(CXX) -o $@ $^ -lbe

# Layout timing for the message view; not part of the app.
bench: $(BUILD)/bench_messageview
$(BUILD)/bench_messageview: $(BUILD)/tests/bench_messageview.o $(BUILD)/src/app/MessageView.o \
		$(BUILD)/src/app/BitmapUtils.o $(BUILD)/src/core/Format.o
	$(CXX) -o $@ $^ -lbe -ltranslation

$(BUILD)/$(NAME).rsrc: src/app/Powergram.rdef
	@mkdir -p $(dir $@)
	rc -o $@ $<
else
all:
	@echo "The app builds on Haiku or Renku only; 'make test' runs the core tests here."
	@false
endif

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/test_core: $(CORE_OBJS) $(BUILD)/tests/test_core.o
	$(CXX) -o $@ $^ -lz

test: $(BUILD)/test_core
	$(BUILD)/test_core

# The login QR code read back by a real QR reader (needs zbarimg).
qr-check: $(BUILD)/qr_dump
	$(BUILD)/qr_dump "tg://login?token=AQKx2PmVZ3c4uVq1_mNbYpTj0mUeQxV9gsQjEz5oW8kRqA" > $(BUILD)/qr.pbm
	test "$$(zbarimg -q --raw $(BUILD)/qr.pbm)" = "tg://login?token=AQKx2PmVZ3c4uVq1_mNbYpTj0mUeQxV9gsQjEz5oW8kRqA"
	@echo "the QR code reads back"
$(BUILD)/qr_dump: $(BUILD)/tests/qr_dump.o $(BUILD)/src/core/QrCode.o \
		$(BUILD)/src/third_party/qrcodegen/qrcodegen.o
	$(CXX) -o $@ $^

clean:
	rm -rf $(BUILD) $(NAME)

-include $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(BUILD)/tests/test_core.d

.PHONY: all test qr-check clean bench live-scroll pickers-demo mockup voice_play video-playback-test media-viewer-demo

voice_play: $(BUILD)/voice_play
$(BUILD)/voice_play: $(BUILD)/tests/voice_play.o $(BUILD)/src/app/VoicePlayer.o
	$(CXX) -o $@ $^ -lbe -lmedia
