#pragma once
namespace RHI
{
	class CommandPool
	{
	public:
		CommandPool();
		~CommandPool();
		virtual auto Reset() -> void = 0;
	private:

	};


};