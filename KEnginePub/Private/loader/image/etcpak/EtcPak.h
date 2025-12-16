#pragma  once

#include <cstdlib>
#include <cstdint>

typedef int BOOL;


namespace ETC_PAK
{
	class TaskDispatch;

	enum TEX_TYPE {
		DXTC,
		ETC2,
		//ETC1 //没有存在的必要，就干掉吧
	};

	BOOL LoadTga(const char* pTgaFile, uint32_t** ppBuffer, uint32_t& pixelCount,
		uint32_t& width, uint32_t& height, BOOL& bHasAlpha, BOOL inversY, BOOL bSwapRB);

	BOOL BuildCompressdFileData(uint8_t** ppOutFileData, size_t& outFileSize, uint32_t* pPixelData,
		uint32_t width, uint32_t height, BOOL bAlpha, BOOL mipmap, TEX_TYPE texType);


	int TestCompress();

	BOOL IsETC(const char* pFileName);

#ifdef _WIN32
	void CheckEtcDirListForWin32(const char *szDirName);
#endif

	class TaskDispatchSingleton
	{
	public:
		static TaskDispatchSingleton& Instance()
		{
			// Since it's a static variable, if the class has already been created,
			// it won't be created again.
			// And it **is** thread-safe in C++11.
			static TaskDispatchSingleton myInstance;

			// Return a reference to our instance.
			return myInstance;
		}

		void DoNothing() {}

		// delete copy and move constructors and assign operators
		TaskDispatchSingleton(TaskDispatchSingleton const&)            = delete; // Copy construct
		TaskDispatchSingleton(TaskDispatchSingleton&&)                 = delete; // Move construct
		TaskDispatchSingleton& operator=(TaskDispatchSingleton const&) = delete; // Copy assign
		TaskDispatchSingleton& operator=(TaskDispatchSingleton&&)      = delete; // Move assign

	protected:
		TaskDispatchSingleton();

		~TaskDispatchSingleton();

		TaskDispatch* m_pTaskDispatch = nullptr;
	};
};
