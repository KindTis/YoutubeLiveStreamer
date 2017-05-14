#pragma once
class YoutubeLiveStreamAPI
{
public:
	YoutubeLiveStreamAPI() = delete;
	YoutubeLiveStreamAPI(std::string title);
	YoutubeLiveStreamAPI(std::string title, std::string token);
	~YoutubeLiveStreamAPI();

	struct ffmpegProc
	{
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
	};

public:
	const std::string& GetToken() { return mToken; }
	void LiveStream(std::wstring fileName);
	void KillStream();

private:
	bool _OAuth(std::string code);
	bool _CreateBroadcast();
	bool _CreateStream();
	bool _BindBroadcast();
	void _UploadStreaming(std::wstring fileName);
	bool _GotoTestTransition();
	bool _GotoLiveTransition();
	
	struct curl_slist* GetHeaderWithToken();
	std::string GetTimeNow();

private:
	std::string mCode;
	std::string mToken;
	std::string mTitle;
	std::string mTokenHeader;

	Json::Value mOAuthResponse;
	Json::Value mBroadcastResponse;
	Json::Value mStreamResponse;
	Json::Value mBindBroadcastResponse;
	Json::Value mBroadcastTransitionResponse;

	ffmpegProc mffmepg;
};

