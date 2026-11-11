// SPDX-License-Identifier: GPL-3.0-or-later
// voice_play FILE: plays a voice message file with VoicePlayer and prints
// its progress, to check the Media Kit path without an account.
#include <cstdio>
#include <cstring>

#include <OS.h>

#include "../src/app/VoicePlayer.h"

int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: voice_play FILE\n");
		return 2;
	}
	VoicePlayer player;
	status_t status = player.Play(argv[1]);
	if (status != B_OK) {
		fprintf(stderr, "play failed: %s\n", strerror(status));
		return 1;
	}
	bigtime_t start = system_time();
	while (player.IsPlaying() && system_time() - start < 30000000) {
		printf("%.2f\n", player.Progress());
		snooze(500000);
	}
	printf("ended after %.1f s, progress %.2f\n", (system_time() - start) / 1e6, player.Progress());
	return 0;
}
