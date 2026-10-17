#pragma once

#include "../Dimps/Dimps__Pad.hxx"

namespace sf4e {
	namespace Pad {
		void Install();
        bool MenuInputBlocked();

		struct System : Dimps::Pad::System
		{
			struct Inputs {
				unsigned int mappedOn;
				unsigned int rawOn;
			};

			static const int PLAYBACK_MAX = 60;
			static Inputs playbackData[PLAYBACK_MAX][2];
			static int playbackFrame;
			static void Install();
            void UpdateInputs();
			void LoadButtonConfig();

			unsigned int GetButtons_RawOn(int pindex);
			unsigned int GetButtons_MappedOn(int pindex);
			// Both hooks above read through this: GGPO playback, then the
			// training override, then the native pad behind the menu capture.
			unsigned int ReadButtons(int pindex, bool raw);
		};

		// The game's keyboard scan, kept to the keyboards its object has room for.
		struct Keyboards : Dimps::Pad::System_RawInput
		{
			static void Install();
			// Once logging is up: what the scan during Main::Initialize found.
			static void LogFirstScan();
			int ScanDevices();
		};
	}
}
