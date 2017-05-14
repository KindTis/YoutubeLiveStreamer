#include "stdafx.h"
#include "YoutubeAPI.h"

const std::string clientID = "clientID";
const std::string clientSecret = "clientSeceret";

std::wstring s2ws(const std::string& s)
{
	int slength = (int)s.length() + 1;
	int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, nullptr, 0);
	std::wstring r(len, L'\0');
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, &r[0], len);
	return r;
}

std::string ws2s(const std::wstring& s)
{
	int slength = (int)s.length() + 1;
	int len = WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, nullptr, 0, nullptr, nullptr);
	std::string r(len, '\0');
	WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, &r[0], len, nullptr, nullptr);
	return r;
}

struct MemoryStruct {
	char *memory;
	size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		/* out of memory! */
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}


void PostCallback(void *ptr, size_t size, size_t nmemb, void *userp)
{
	std::string *str = (std::string*)userp;
	str->append((char*)ptr, size * nmemb);
}

YoutubeLiveStreamAPI::YoutubeLiveStreamAPI(std::string title) : mTitle(title)
{

}

YoutubeLiveStreamAPI::YoutubeLiveStreamAPI(std::string title, std::string token) : mTitle(title), mToken(token)
{

}

YoutubeLiveStreamAPI::~YoutubeLiveStreamAPI()
{
	KillStream();
}

void YoutubeLiveStreamAPI::LiveStream(std::wstring fileName)
{
	if (GetToken().length() == 0)
	{
		std::string authUrl = "https://accounts.google.com/o/oauth2/v2/auth?scope=https://www.googleapis.com/auth/youtube%20https://www.googleapis.com/auth/youtube.force-ssl&redirect_uri=urn:ietf:wg:oauth:2.0:oob:auto&access_type=offline&response_type=code&client_id=" + clientID;

		ShellExecuteA(nullptr, "open", authUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

		std::cout << "Input Auth Code: ";
		std::cin >> mCode;

		if (!_OAuth(mCode))
		{
			std::cout << "Failed to Get Access Token" << std::endl;
			return;
		}
	}

	if (!_CreateBroadcast())
	{
		std::cout << "Failed to Create Broadcast" << std::endl;
		return;
	}

	if (!_CreateStream())
	{
		std::cout << "Failed to Create Stream" << std::endl;
		return;
	}

	if (!_BindBroadcast())
	{
		std::cout << "Failed to Bind Broadcast" << std::endl;
		return;
	}

	_UploadStreaming(fileName);

	// check stream inactive

	//if (!_GotoTestTransition())
	//{
	//	std::cout << "Failed to Broadcast Live Transition" << std::endl;
	//	return;
	//}

	// check broadcast testing

	//if (!_GotoLiveTransition())
	//{
	//	std::cout << "Failed to Broadcast Live Transition" << std::endl;
	//	return;
	//}
}

bool YoutubeLiveStreamAPI::_OAuth(std::string code)
{
	CURL *curl;
	CURLcode res;

	std::string strResult;

	struct curl_httppost *formpost = nullptr;
	struct curl_httppost *lastptr = nullptr;

	curl_global_init(CURL_GLOBAL_ALL);

	curl_formadd(&formpost,
		&lastptr,
		CURLFORM_COPYNAME, "code",
		CURLFORM_COPYCONTENTS, code.c_str(),
		CURLFORM_END);

	curl_formadd(&formpost,
		&lastptr,
		CURLFORM_COPYNAME, "client_id",
		CURLFORM_COPYCONTENTS, clientID.c_str(),
		CURLFORM_END);

	curl_formadd(&formpost,
		&lastptr,
		CURLFORM_COPYNAME, "client_secret",
		CURLFORM_COPYCONTENTS, clientSecret.c_str(),
		CURLFORM_END);

	curl_formadd(&formpost,
		&lastptr,
		CURLFORM_COPYNAME, "redirect_uri",
		CURLFORM_COPYCONTENTS, "urn:ietf:wg:oauth:2.0:oob:auto",
		CURLFORM_END);

	curl_formadd(&formpost,
		&lastptr,
		CURLFORM_COPYNAME, "grant_type",
		CURLFORM_COPYCONTENTS, "authorization_code",
		CURLFORM_END);


	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, "https://accounts.google.com/o/oauth2/token");
		curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &strResult);

		res = curl_easy_perform(curl);
		if (res != CURLE_OK && res != CURLE_WRITE_ERROR)
		{
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			return false;
		}

		curl_easy_cleanup(curl);
		curl_formfree(formpost);

		std::cout << strResult << std::endl;

		Json::Reader reader;
		reader.parse(strResult, mOAuthResponse);
		mToken = mOAuthResponse["access_token"].asString();
		return true;
	}
	return false;
}

bool YoutubeLiveStreamAPI::_CreateBroadcast()
{
	CURL *curl;
	CURLcode res;

	std::string strTargetURL;
	std::string strResourceJSON;
	std::string strResult;
	std::string strScheduledStartTime = GetTimeNow();

	struct curl_httppost *lastptr = nullptr;
	struct curl_slist *headerlist = GetHeaderWithToken();

	MemoryStruct chunk;
	chunk.memory = (char*)malloc(1);
	chunk.size = 0;

	strTargetURL = "https://www.googleapis.com/youtube/v3/liveBroadcasts?part=snippet%2Cstatus";
	strResourceJSON = "{\"snippet\": {\"title\": \"" + mTitle + "\", \"scheduledStartTime\": \"" + strScheduledStartTime + "\"},\"status\": {\"privacyStatus\": \"private\"}}";

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, strTargetURL.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);

		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strResourceJSON.c_str());

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

		res = curl_easy_perform(curl);
		if (res != CURLE_OK && res != CURLE_WRITE_ERROR)
		{
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			return false;
		}

		curl_easy_cleanup(curl);
		curl_slist_free_all(headerlist);

		std::cout << "------------Create Broadcast Result" << std::endl;
		std::cout << chunk.memory << std::endl;

		Json::Reader reader;
		if (!reader.parse(chunk.memory, mBroadcastResponse) ||
			!mBroadcastResponse["id"].isString())
		{
			std::cout << "Failed to Parse Broadcast JSON" << std::endl;
			return false;
		}
		return true;
	}
	return false;
}

bool YoutubeLiveStreamAPI::_CreateStream()
{
	CURL *curl;
	CURLcode res;

	std::string strTargetURL;
	std::string strResourceJSON;
	std::string strResult;

	struct curl_slist *headerlist = GetHeaderWithToken();

	strTargetURL = "https://www.googleapis.com/youtube/v3/liveStreams?part=snippet%2Ccdn";
	strResourceJSON = "{\"snippet\": {\"title\": \"" + mTitle + "\"},\"cdn\" : {\"format\": \"720p\",\"ingestionType\" : \"rtmp\"}}";
	
	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, strTargetURL.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);

		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strResourceJSON.c_str());

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &strResult);

		res = curl_easy_perform(curl);
		if (res != CURLE_OK && res != CURLE_WRITE_ERROR)
		{
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			return false;
		}

		curl_easy_cleanup(curl);
		curl_slist_free_all(headerlist);

		std::cout << "------------Create Stream Result" << std::endl;
		std::cout << strResult << std::endl;

		Json::Reader reader;
		if (!reader.parse(strResult, mStreamResponse) ||
			!mStreamResponse["id"].isString())
		{
			std::cout << "Failed to Parse Broadcast JSON" << std::endl;
			return false;
		}
		return true;
	}
	return false;
}

bool YoutubeLiveStreamAPI::_BindBroadcast()
{
	CURL *curl;
	CURLcode res;
	
	std::string strTargetURL;
	std::string strParameter;
	std::string strResult;

	struct curl_slist *headerlist = GetHeaderWithToken();
	
	strTargetURL = "https://www.googleapis.com/youtube/v3/liveBroadcasts/bind";
	strParameter = "?id=" + mBroadcastResponse["id"].asString() + "&part=id%2CcontentDetails&streamId=" + mStreamResponse["id"].asString();
	strTargetURL = strTargetURL + strParameter;

	curl_global_init(CURL_GLOBAL_ALL);
	
	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, strTargetURL.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);
		
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &strResult);

		res = curl_easy_perform(curl);
		if (res != CURLE_OK && res != CURLE_WRITE_ERROR)
		{
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			return false;
		}

		curl_easy_cleanup(curl);

		std::cout << "------------BindBroadcast Result" << std::endl;
		std::cout << strResult << std::endl;

		Json::Reader reader;
		if (!reader.parse(strResult, mBindBroadcastResponse) ||
			!mBindBroadcastResponse["id"].isString())
		{
			std::cout << "Failed to Parse Bind Broadcast JSON" << std::endl;
			return false;
		}
		return true;
	}
	return false;
}

void YoutubeLiveStreamAPI::_UploadStreaming(std::wstring fileName)
{
	ZeroMemory(&mffmepg.si, sizeof(mffmepg.si));
	mffmepg.si.cb = sizeof(mffmepg.si);
	ZeroMemory(&mffmepg.pi, sizeof(mffmepg.pi));

	std::string targetURL = mStreamResponse["cdn"]["ingestionInfo"]["ingestionAddress"].asString();
	std::string streamName = mStreamResponse["cdn"]["ingestionInfo"]["streamName"].asString();
	
	std::string fullUrl = targetURL + "/" + streamName;
	std::wstring wfullUrl = s2ws(fullUrl);

	std::wstring cmd = L"ffmpeg -re -i \"" + fileName + L"\" -c:v libx264 -preset medium -maxrate 3000k -bufsize 6000k -pix_fmt yuv420p -c:a aac -b:a 128k -ac 2 -ar 44100 -threads 4 -f flv "  + wfullUrl;

	std::cout << "------------Start Live Streaming" << std::endl;
	std::wcout << cmd << std::endl;

	CreateProcess(nullptr, &cmd[0], nullptr, nullptr, false, CREATE_NO_WINDOW, nullptr, nullptr, &mffmepg.si, &mffmepg.pi);
}

bool YoutubeLiveStreamAPI::_GotoTestTransition()
{
	CURL *curl;
	CURLcode res;

	std::string strTargetURL;
	std::string strParameter;
	std::string strResult;

	struct curl_slist *headerlist = GetHeaderWithToken();

	strTargetURL = "https://www.googleapis.com/youtube/v3/liveBroadcasts/transition";
	strParameter = "?broadcastStatus=testing&id=" + mBroadcastResponse["id"].asString() + "&part=id";

	strTargetURL = strTargetURL + strParameter;

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, strTargetURL.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &strResult);

		res = curl_easy_perform(curl);
		if (res != CURLE_OK && res != CURLE_WRITE_ERROR)
		{
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			return false;
		}

		curl_easy_cleanup(curl);

		std::cout << "------------Broadcast Transition Result" << std::endl;
		std::cout << strResult << std::endl;

		Json::Reader reader;
		if (!reader.parse(strResult, mBroadcastTransitionResponse) ||
			!mBroadcastTransitionResponse["id"].isString())
		{
			std::cout << "Failed to Parse Broadcast Transition JSON" << std::endl;
			return false;
		}
		return true;
	}
	return false;
}

bool YoutubeLiveStreamAPI::_GotoLiveTransition()
{
	CURL *curl;
	CURLcode res;

	std::string strTargetURL;
	std::string strParameter;
	std::string strResult;

	struct curl_slist *headerlist = GetHeaderWithToken();

	strTargetURL = "https://www.googleapis.com/youtube/v3/liveBroadcasts/transition";
	strParameter = "?broadcastStatus=live&id=" + mBroadcastResponse["id"].asString() + "&part=id";

	strTargetURL = strTargetURL + strParameter;

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, strTargetURL.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &strResult);

		res = curl_easy_perform(curl);
		if (res != CURLE_OK && res != CURLE_WRITE_ERROR)
		{
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			return false;
		}

		curl_easy_cleanup(curl);

		std::cout << "------------Broadcast Transition Result" << std::endl;
		std::cout << strResult << std::endl;

		Json::Reader reader;
		if (!reader.parse(strResult, mBroadcastTransitionResponse) ||
			!mBroadcastTransitionResponse["id"].isString())
		{
			std::cout << "Failed to Parse Broadcast Transition JSON" << std::endl;
			return false;
		}
		return true;
	}
	return false;
}

struct curl_slist* YoutubeLiveStreamAPI::GetHeaderWithToken()
{
	std::string token = mOAuthResponse["access_token"].asString();
	std::string tokenType = mOAuthResponse["token_type"].asString();
	std::string accessTokenHeader = "Authorization: " + tokenType + " " + token;

	struct curl_slist *headerlist = nullptr;

	headerlist = curl_slist_append(headerlist, "Content-Type: application/json");
	headerlist = curl_slist_append(headerlist, accessTokenHeader.c_str());

	return headerlist;
}

void YoutubeLiveStreamAPI::KillStream()
{
	DWORD dwExitCode;
	if (mffmepg.pi.hProcess != nullptr)
		GetExitCodeProcess(mffmepg.pi.hProcess, &dwExitCode);

	if (dwExitCode == STILL_ACTIVE)
	{
		TerminateProcess(mffmepg.pi.hProcess, 0);
		CloseHandle(mffmepg.pi.hProcess);
	}
}

std::string YoutubeLiveStreamAPI::GetTimeNow()
{
	char mbstr[64];
	std::time_t t = std::time(nullptr);
	std::tm currTM;

	localtime_s(&currTM, &t);
	std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%dT%H:%M:%S%z", &currTM);

	std::string ISO8601TimeFormat;
	ISO8601TimeFormat.append(mbstr);

	return ISO8601TimeFormat;
}