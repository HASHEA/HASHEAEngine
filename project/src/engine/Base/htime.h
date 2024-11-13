#pragma once
#include "hcore.h"
#include "hplatform.h"
namespace HASHEAENGINE
{
	void	time_service_init();                // Needs to be called once at startup.
	void	time_service_shutdown();           // Needs to be called at shutdown.
	int64_t	HASHEA_API	time_now();                         // Get current time ticks.
	double	HASHEA_API	time_microseconds(int64_t time);  // Get microseconds from time ticks
	double	HASHEA_API	time_milliseconds(int64_t time);  // Get milliseconds from time ticks
	double	HASHEA_API	time_seconds(int64_t time);       // Get seconds from time ticks
	int64_t	HASHEA_API	time_from(int64_t starting_time); // Get time difference from start to current time.
	double	HASHEA_API	time_from_microseconds(int64_t starting_time); // Convenience method.
	double	HASHEA_API	time_from_milliseconds(int64_t starting_time); // Convenience method.
	double	HASHEA_API	time_from_seconds(int64_t starting_time);      // Convenience method.
	double	HASHEA_API	time_delta_seconds(int64_t starting_time, int64_t ending_time);
	double	HASHEA_API	time_delta_milliseconds(int64_t starting_time, int64_t ending_time);
};