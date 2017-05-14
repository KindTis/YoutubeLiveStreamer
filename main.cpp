#include "YoutubeAPI.h"

int main(int argc, char **argv)
{	
	YoutubeLiveStreamAPI youapi("test live stream", "");
	youapi.LiveStream(L"M2U00071.MPG");

	std::string input;
	std::cout << "Stop Streaming: \"s\"" << std::endl;
	std::cout << "Exit Programming: \"e\"" << std::endl;
	while (input != "e")
	{
		std::cin >> input;
		if (input == "s")
		{
			std::cout << "Kill ffmpeg" << std::endl;
			youapi.KillStream();
		}
	}

	system("pause");
	return 0;
}