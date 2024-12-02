#pragma once
namespace RHI
{
	class CommandPool
	{
	public:
		CommandPool() =default;
		virtual ~CommandPool() {};
		virtual auto reset() -> void = 0;
	private:

	};


};