# Paper Plane for Haiku.
#   make          build the app (on Haiku/Renku)
#   make test     portable core tests (anywhere with a C++17 compiler)

NAME := PaperPlane
CXX ?= g++
override CXXFLAGS += -std=c++17 -Wall -Wextra -O2

CORE_SRCS := src/core/AuthFlow.cpp src/core/ChatList.cpp src/core/ChatHistory.cpp \
	src/core/SendQueue.cpp src/core/Files.cpp src/core/Stickers.cpp
APP_SRCS := src/app/App.cpp src/app/TdClient.cpp src/app/Settings.cpp \
	src/app/LoginWindow.cpp src/app/MainWindow.cpp src/app/MessageView.cpp \
	src/app/BitmapUtils.cpp src/app/Icons.cpp src/app/Pickers.cpp

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

# The app at ./PaperPlane, resources from obj/ (packaging/build-hpkg.sh).
$(NAME): $(CORE_OBJS) $(APP_OBJS) $(BUILD)/$(NAME).rsrc
	$(CXX) -o $@ $(CORE_OBJS) $(APP_OBJS) $(LDFLAGS) -lbe -ltracker -ltranslation -llocalestub $(TD_LIBS)
	xres -o $@ $(BUILD)/$(NAME).rsrc
	mimeset -f $@

# The real main window with made-up chats, for screenshots.
MOCKUP_OBJS := $(filter-out $(BUILD)/src/app/App.o $(BUILD)/src/app/TdClient.o \
	$(BUILD)/src/app/LoginWindow.o $(BUILD)/src/app/Settings.o, $(APP_OBJS))
mockup: $(BUILD)/PaperPlaneMockup
$(BUILD)/PaperPlaneMockup: $(BUILD)/tests/mockup.o $(CORE_OBJS) $(MOCKUP_OBJS)
	$(CXX) -o $@ $^ -lbe -ltracker -ltranslation -llocalestub

# The entry's icon buttons and a picker with made-up content.
pickers-demo: $(BUILD)/pickers_demo
$(BUILD)/pickers_demo: $(BUILD)/tests/pickers_demo.o $(BUILD)/src/app/Pickers.o \
		$(BUILD)/src/app/Icons.o $(BUILD)/src/core/Stickers.o
	$(CXX) -o $@ $^ -lbe -ltranslation

# Scroll position through real resizes, in a visible window.
live-scroll: $(BUILD)/live_scroll
$(BUILD)/live_scroll: $(BUILD)/tests/live_scroll.o $(BUILD)/src/app/MessageView.o \
		$(BUILD)/src/app/BitmapUtils.o
	$(CXX) -o $@ $^ -lbe -ltranslation

# Layout timing for the message view; not part of the app.
bench: $(BUILD)/bench_messageview
$(BUILD)/bench_messageview: $(BUILD)/tests/bench_messageview.o $(BUILD)/src/app/MessageView.o \
		$(BUILD)/src/app/BitmapUtils.o
	$(CXX) -o $@ $^ -lbe -ltranslation

$(BUILD)/$(NAME).rsrc: src/app/PaperPlane.rdef
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

.PHONY: all test clean bench live-scroll pickers-demo mockup
