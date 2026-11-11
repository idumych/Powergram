# Powergram, a Telegram client for Haiku.
#   make          build the app (on Haiku/Renku)
#   make test     portable core tests (anywhere with a C++17 compiler)

NAME := Powergram
CXX ?= g++
override CXXFLAGS += -std=c++17 -Wall -Wextra -O2

CORE_SRCS := src/core/AuthFlow.cpp src/core/ChatList.cpp src/core/ChatHistory.cpp \
	src/core/SendQueue.cpp src/core/Files.cpp src/core/Stickers.cpp \
	src/core/Format.cpp src/core/Presence.cpp src/core/ChatInfo.cpp \
	src/core/Links.cpp src/core/Pinned.cpp src/core/VoiceNote.cpp src/core/Mentions.cpp
APP_SRCS := src/app/App.cpp src/app/TdClient.cpp src/app/Settings.cpp \
	src/app/LoginWindow.cpp src/app/MainWindow.cpp src/app/MessageView.cpp \
	src/app/BitmapUtils.cpp src/app/Icons.cpp src/app/Pickers.cpp src/app/ChatPicker.cpp \
	src/app/SettingsWindow.cpp src/app/VoicePlayer.cpp \
	src/app/ChatInfoWindow.cpp src/app/FileSaving.cpp src/app/OpenLinkWindow.cpp \
	src/app/VoiceRecorder.cpp src/app/MentionPopup.cpp

BUILD := obj
CORE_OBJS := $(CORE_SRCS:%.cpp=$(BUILD)/%.o)
APP_OBJS := $(APP_SRCS:%.cpp=$(BUILD)/%.o)

# TDLib from HaikuPorts ships static libraries only; this is the order its
# CMake targets give (TdStaticTargets.cmake: Td::TdJsonStatic).
TD_LIBS := -ltdjson_static -ltdjson_private -ltdclient -ltdcore -ltdapi \
	-ltdmtproto -ltdnet -ltddb -ltdsqlite -ltde2e -ltdactor -ltdutils \
	-lssl -lcrypto -lz -lnetwork

ifeq ($(shell uname -s),Haiku)
all: $(NAME)

# The app at ./Powergram, resources from obj/ (packaging/build-hpkg.sh).
$(NAME): $(CORE_OBJS) $(APP_OBJS) $(BUILD)/$(NAME).rsrc
	$(CXX) -o $@ $(CORE_OBJS) $(APP_OBJS) $(LDFLAGS) -lbe -lmedia -ltracker -ltranslation -llocalestub -lopus -logg $(TD_LIBS)
	xres -o $@ $(BUILD)/$(NAME).rsrc
	mimeset -f $@

# The real main window with made-up chats, for screenshots.
MOCKUP_OBJS := $(filter-out $(BUILD)/src/app/App.o $(BUILD)/src/app/TdClient.o \
	$(BUILD)/src/app/LoginWindow.o $(BUILD)/src/app/Settings.o, $(APP_OBJS))
mockup: $(BUILD)/PowergramMockup
$(BUILD)/PowergramMockup: $(BUILD)/tests/mockup.o $(CORE_OBJS) $(MOCKUP_OBJS)
	$(CXX) -o $@ $^ -lbe -lmedia -ltracker -ltranslation -llocalestub -lopus -logg

# The entry's icon buttons and a picker with made-up content.
pickers-demo: $(BUILD)/pickers_demo
$(BUILD)/pickers_demo: $(BUILD)/tests/pickers_demo.o $(BUILD)/src/app/Pickers.o \
		$(BUILD)/src/app/Icons.o $(BUILD)/src/core/Stickers.o
	$(CXX) -o $@ $^ -lbe -ltranslation

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
	$(CXX) -o $@ $^

test: $(BUILD)/test_core
	$(BUILD)/test_core

clean:
	rm -rf $(BUILD) $(NAME)

-include $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(BUILD)/tests/test_core.d

.PHONY: all test clean bench live-scroll pickers-demo mockup voice_play

voice_play: $(BUILD)/voice_play
$(BUILD)/voice_play: $(BUILD)/tests/voice_play.o $(BUILD)/src/app/VoicePlayer.o
	$(CXX) -o $@ $^ -lbe -lmedia
